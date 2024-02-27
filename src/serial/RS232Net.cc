#include "RS232Net.hh"
#include "RS232Connector.hh"
#include "PlugException.hh"
#include "EventDistributor.hh"
#include "Scheduler.hh"
#include "FileOperations.hh"
#include "checked_cast.hh"
#include "narrow.hh"
#include "Socket.hh"
#include "serialize.hh"
#include "ranges.hh"
#include "StringOp.hh"
#include <array>
#ifndef _WIN32
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#endif

namespace openmsx {

// IP232 protocol
static constexpr char IP232_MAGIC    = '\xff';

// sending
static constexpr char IP232_DTR_LO   = '\x00';
static constexpr char IP232_DTR_HI   = '\x01';

// receiving
static constexpr char IP232_DCD_LO   = '\x00';
static constexpr char IP232_DCD_HI   = '\x01';
static constexpr char IP232_DCD_MASK = '\x01';

static constexpr char IP232_RI_LO    = '\x00';
static constexpr char IP232_RI_HI    = '\x02';
static constexpr char IP232_RI_MASK  = '\x02';

RS232Net::RS232Net(EventDistributor& eventDistributor_,
                         Scheduler& scheduler_,
                         CommandController& commandController)
	: eventDistributor(eventDistributor_), scheduler(scheduler_)
	, rs232NetAddressSetting(
	        commandController, "rs232-net-address",
	        "IP/address:port for RS232 net pluggable",
	        "127.0.0.1:25232")
	, rs232NetUseIP232(
	        commandController, "rs232-net-ip232",
	        "Enable IP232 protocol",
	        true)
{
	eventDistributor.registerEventListener(EventType::RS232_NET, *this);
}

RS232Net::~RS232Net()
{
	eventDistributor.unregisterEventListener(EventType::RS232_NET, *this);
}

// Pluggable
void RS232Net::plugHelper(Connector& connector_, EmuTime::param /*time*/)
{
	if (!network_address_generate()){
		throw PlugException("Incorrect address / could not resolve: ", rs232NetAddressSetting.getString());
	}
	open_socket();
	if (sockfd == OPENMSX_INVALID_SOCKET) {
		throw PlugException("Can't open connection");
	}

	DTR = false;
	RTS = true;
	DCD = false;
	RI  = false;
	IP232 = rs232NetUseIP232.getBoolean();

	auto& rs232Connector = checked_cast<RS232Connector&>(connector_);
	rs232Connector.setDataBits(SerialDataInterface::DATA_8); // 8 data bits
	rs232Connector.setStopBits(SerialDataInterface::STOP_1); // 1 stop bit
	rs232Connector.setParityBit(false, SerialDataInterface::EVEN); // no parity

	setConnector(&connector_); // base class will do this in a moment,
	                           // but thread already needs it
	thread = std::thread([this]() { run(); });
}

void RS232Net::unplugHelper(EmuTime::param /*time*/)
{
	// close socket
	if (sockfd != OPENMSX_INVALID_SOCKET) {
		if (IP232) {
			net_putc(IP232_MAGIC);
			net_putc(IP232_DTR_LO);
		}
		sock_close(sockfd);
		sockfd = OPENMSX_INVALID_SOCKET;
	}
	// stop helper thread
	poller.abort();
	thread.join();
}

std::string_view RS232Net::getName() const
{
	return "rs232-net";
}

std::string_view RS232Net::getDescription() const
{
	return "RS232 Network pluggable. Connects the RS232 port to IP:PORT, "
	       "selected with the 'rs232-net-address' setting.";
}

void RS232Net::run()
{
	bool ipMagic = false;
	while (true) {
		if (poller.aborted() || (sockfd == OPENMSX_INVALID_SOCKET)) {
			break;
		}

		auto b = net_getc();
		if (!b) continue;

		if (IP232) {
			if (ipMagic) {
				ipMagic = false;
				if (*b != IP232_MAGIC) {
					DCD = (*b & IP232_DCD_MASK) == IP232_DCD_HI;
					// RI implemented in TCPSer 1.1.5 (not yet released)
					// RI present at least on Sony HBI-232 and HB-G900AP (bit 1 of &H82/&HBFFA status register)
					//    missing on SVI-738
					RI = (*b & IP232_RI_MASK) == IP232_RI_HI;
					continue;
				}
				// was a literal 0xff
			} else {
				if (*b == IP232_MAGIC) {
					ipMagic = true;
					continue;
				}
			}
		}

		assert(isPluggedIn());
		std::lock_guard<std::mutex> lock(mutex);
		queue.push_back(*b);
		eventDistributor.distributeEvent(Rs232NetEvent());
	}
}

// input
void RS232Net::signal(EmuTime::param time)
{
	auto* conn = checked_cast<RS232Connector*>(getConnector());

	if (!conn->acceptsData()) {
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
		return;
	}

	if (!conn->ready() || !RTS) return;

	std::lock_guard<std::mutex> lock(mutex);
	if (queue.empty()) return;
	char b = queue.pop_front();
	conn->recvByte(b, time);
}

// EventListener
int RS232Net::signalEvent(const Event& /*event*/)
{
	if (isPluggedIn()) {
		signal(scheduler.getCurrentTime());
	} else {
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
	}
	return 0;
}

// output
void RS232Net::recvByte(uint8_t value_, EmuTime::param /*time*/)
{
	auto value = static_cast<char>(value_);
	if ((value == IP232_MAGIC) && IP232) {
		net_putc(IP232_MAGIC);
	}
	net_putc(value);
}

// Control lines
bool RS232Net::getDCD(EmuTime::param /*time*/) const
{
	return DCD;
}

void RS232Net::setDTR(bool status, EmuTime::param /*time*/)
{
	if (DTR == status) return;
	DTR = status;
	if (IP232) {
		if (sockfd != OPENMSX_INVALID_SOCKET) {
			net_putc(IP232_MAGIC);
			net_putc(DTR ? IP232_DTR_HI : IP232_DTR_LO);
		}
	}
}

void RS232Net::setRTS(bool status, EmuTime::param /*time*/)
{
	if (RTS == status) return;
	RTS = status;
	if (RTS) {
		std::lock_guard<std::mutex> lock(mutex);
		if (!queue.empty()) {
			eventDistributor.distributeEvent(Rs232NetEvent());
		}
	}
}

// Socket routines below based on VICE emulator socket.c
bool RS232Net::net_putc(char b)
{
	if (sockfd == OPENMSX_INVALID_SOCKET) return false;

	if (auto n = sock_send(sockfd, &b, 1); n < 0) {
		sock_close(sockfd);
		sockfd = OPENMSX_INVALID_SOCKET;
		return false;
	}
	return true;
}

std::optional<char> RS232Net::net_getc()
{
	if (sockfd == OPENMSX_INVALID_SOCKET)  return {};
	if (selectPoll(sockfd) <= 0) return {};
	char b;
	if (sock_recv(sockfd, &b, 1) != 1) {
		sock_close(sockfd);
		sockfd = OPENMSX_INVALID_SOCKET;
		return {};
	}
	return b;
}

// Check if the socket has incoming data to receive
// Returns: 1 if the specified socket has data; 0 if it does not contain
// any data, and -1 in case of an error.
int RS232Net::selectPoll(SOCKET readSock)
{
	struct timeval timeout = {0, 0};

	fd_set sdSocket;
	FD_ZERO(&sdSocket);
	FD_SET(readSock, &sdSocket);

	return select(readSock + 1, &sdSocket, nullptr, nullptr, &timeout);
}

// Open a socket and initialise it for client operation
//
//      The socket_address variable determines the type of
//      socket to be used (IPv4, IPv6, Unix Domain Socket, ...)
void RS232Net::open_socket()
{
	sockfd = socket(socket_address.domain, SOCK_STREAM, socket_address.protocol);
	if (sockfd == OPENMSX_INVALID_SOCKET) return;

	int one = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));

	if (connect(sockfd, &socket_address.address.generic, socket_address.len) < 0) {
		sock_close(sockfd);
		sockfd = OPENMSX_INVALID_SOCKET;
	}
}

// Initialise a socket address structure
void RS232Net::initialize_socket_address()
{
	memset(&socket_address, 0, sizeof(socket_address));
	socket_address.used = 1;
	socket_address.len = sizeof(socket_address.address);
}

// Initialises a socket address with an IPv6 or IPv4 address.
//
// Returns 'true' on success, 'false' when an error occurred.
//
// The address_string must be specified in one of the forms:
//   <host>
//   <host>:port
//   [<hostipv6>]:<port>
// with:
//   <hostname> being the name of the host,
//   <hostipv6> being the IP of the host, and
//   <host>     being the name of the host or its IPv6,
//   <port>     being the port number.
//
// The extra braces [...] in case the port is specified are needed as IPv6
// addresses themselves already contain colons, and it would be impossible to
// clearly distinguish between an IPv6 address, and an IPv6 address where a port
// has been specified. This format is a common one.
bool RS232Net::network_address_generate()
{
	std::string_view address = rs232NetAddressSetting.getString();
	if (address.empty()) {
		// There was no address given, do not try to process it.
		return false;
	}

	// Preset the socket address
	memset(&socket_address.address, 0, sizeof(socket_address.address));
	socket_address.protocol = IPPROTO_TCP;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	auto setIPv4 = [&] {
		hints.ai_family = AF_INET;
		socket_address.domain = PF_INET;
		socket_address.len = sizeof(socket_address.address.ipv4);
		socket_address.address.ipv4.sin_family = AF_INET;
		socket_address.address.ipv4.sin_port = 0;
		socket_address.address.ipv4.sin_addr.s_addr = INADDR_ANY;
	};
	auto setIPv6 = [&] {
		hints.ai_family = AF_INET6;
		socket_address.domain = PF_INET6;
		socket_address.len = sizeof(socket_address.address.ipv6);
		socket_address.address.ipv6.sin6_family = AF_INET6;
		socket_address.address.ipv6.sin6_port = 0;
		socket_address.address.ipv6.sin6_addr = in6addr_any;
	};

	// Parse 'address', fills in 'addressPart' and 'portPart'
	std::string addressPart;
	std::string portPart;
	auto [ipv6_address_part, ipv6_port_part] = StringOp::splitOnFirst(address, ']');
	if (!ipv6_port_part.empty()) { // there was a ']' character (and it's already dropped)
		// Try to parse as: "[IPv6]:port"
		if (!ipv6_address_part.starts_with('[') || !ipv6_port_part.starts_with(':')) {
			// Malformed address, do not try to process it.
			return false;
		}
		addressPart = std::string(ipv6_address_part.substr(1)); // drop '['
		portPart    = std::string(ipv6_port_part   .substr(1)); // drop ':'
		setIPv6();
	} else {
		auto numColons = ranges::count(address, ':');
		if (numColons == 0) {
			// either IPv4 or IPv6
			addressPart = std::string(address);
		} else if (numColons == 1) {
			// ipv4:port
			auto [ipv4_address_part, ipv4_port_part] = StringOp::splitOnFirst(address, ':');
			addressPart = std::string(ipv4_address_part);
			portPart    = std::string(ipv4_port_part);
			setIPv4();
		} else {
			// ipv6 address
			addressPart = std::string(address);
			setIPv6();
		}
	}

	// Interpret 'addressPart' and 'portPart' (possibly the latter is empty)
	struct addrinfo* res;
	if (getaddrinfo(addressPart.c_str(), portPart.c_str(), &hints, &res) != 0) {
		return false;
	}

	memcpy(&socket_address.address, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	return true;
}

template<typename Archive>
void RS232Net::serialize(Archive& /*ar*/, unsigned /*version*/)
{
	// don't try to resume a previous logfile (see PrinterPortLogger)
}
INSTANTIATE_SERIALIZE_METHODS(RS232Net);
REGISTER_POLYMORPHIC_INITIALIZER(Pluggable, RS232Net, "RS232Net");

} // namespace openmsx
