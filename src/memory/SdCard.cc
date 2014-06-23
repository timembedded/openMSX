#include "SdCard.hh"
#include "SRAM.hh"
#include "memory.hh"
#include "unreachable.hh"
#include "endian.hh"
#include <string>

// TODO:
// - use HD instead of SRAM?
//   - then also decide on which constructor args we need like name
// - check behaviour of case where /CS = 1
// - replace transferDelayCounter with 0xFF's in responseQueue? What to do with
//   reset command which clears the queue?
// - remove duplication between READ/WRITE and READ_MULTI/WRITE_MULTI (is it worth it?)
// - implement serialize stuff

namespace openmsx {

// data response tokens
static const byte DRT_ACCEPTED    = 0x05;
static const byte DRT_WRITE_ERROR = 0x0D;

// start block tokens and stop tran token
static const byte START_BLOCK_TOKEN     = 0xFE;
static const byte START_BLOCK_TOKEN_MBW = 0xFC;
static const byte STOP_TRAN_TOKEN       = 0xFD;

// data error token
static const byte DATA_ERROR_TOKEN_OUT_OF_RANGE = 0x40;

// responses
static const byte R1_BUSY            = 0x00;
static const byte R1_IDLE            = 0x01; // TODO: why is lots of code checking for this instead of R1_BUSY?
static const byte R1_ILLEGAL_COMMAND = 0x04;
static const byte R1_PARAMETER_ERROR = 0x80;

SdCard::SdCard(const DeviceConfig& config, const std::string& name_)
	: ram(config.getXML() == nullptr ? nullptr : make_unique<SRAM>(name_ + "SD flash", config.getChildDataAsInt("size", 100) * 1024 * 1024, config))
	, name(name_)
	, transferDelayCounter(0)
	, mode(COMMAND)
	, currentSector(0)
	, currentByteInSector(0)
{
	reset();
}

SdCard::~SdCard()
{
}

void SdCard::reset()
{
	cmdIdx = 0;
	responseQueue.clear();
	mode = COMMAND;
}

byte SdCard::transfer(byte value, bool cs)
{
	if (!ram) return 0xFF; // no card inserted

	if (cs) {
		// /CS is true: not for this chip
		reset();
		return 0xFF;
	}

	// process output
	byte retval = 0xFF;
	if (transferDelayCounter > 0) {
		--transferDelayCounter;
	} else {
		if (responseQueue.empty()) {
			switch (mode) {
			case READ:
				if (currentByteInSector == -1) {
					retval = START_BLOCK_TOKEN;
				} else {
					// output next byte from stream
					retval = (*ram)[currentSector * SECTOR_SIZE + currentByteInSector];
				}
				currentByteInSector++;
				if (currentByteInSector == SECTOR_SIZE) {
					responseQueue.push_back(0x00); // CRC 1 (dummy)
					responseQueue.push_back(0x00); // CRC 2 (dummy)	
					mode = COMMAND;
				}
				break;
			case MULTI_READ:
				if (currentSector >= (ram->getSize() / SECTOR_SIZE)) {
					// data out of range, send data error token
					retval = DATA_ERROR_TOKEN_OUT_OF_RANGE;
					mode = COMMAND; // TODO: verify this (how?)
				} else {
					if (currentByteInSector == -1) {
						retval = START_BLOCK_TOKEN;
					} else {
						// output next byte from stream
						retval = (*ram)[currentSector * SECTOR_SIZE + currentByteInSector];
					}
					currentByteInSector++;
					if (currentByteInSector == SECTOR_SIZE) {
						currentSector++;
						currentByteInSector = -1;
						responseQueue.push_back(0x00); // CRC 1 (dummy)
						responseQueue.push_back(0x00); // CRC 2 (dummy)	
					}
				}
				break;
			case WRITE:
			case MULTI_WRITE:
				retval = R1_BUSY;
				break;
			case COMMAND:
			default:
			break;
			}
		} else {
			retval = responseQueue.pop_front();
		}
	}

	// process input
	switch (mode) {
	case WRITE:
		// first check for data token
		if (currentByteInSector == -1) {
			if (value == START_BLOCK_TOKEN) {
				currentByteInSector++;
			}
			break;
		}
		if (currentByteInSector < SECTOR_SIZE) {
			sectorBuf[currentByteInSector] = value;
		}
		currentByteInSector++;
		if (currentByteInSector == (SECTOR_SIZE + 2)) {
			// copy buffer to SD card
			for (unsigned bc = 0; bc < SECTOR_SIZE; bc++) {
				ram->write(currentSector * SECTOR_SIZE + bc, sectorBuf[bc]);
			}
			mode = COMMAND;
			transferDelayCounter = 1;
			responseQueue.push_back(DRT_ACCEPTED);
		}
		break;
	case MULTI_WRITE:
		// first check for stop or start token
		if (currentByteInSector == -1) {
			if (value == STOP_TRAN_TOKEN) {
				mode = COMMAND;
			}
			if (value == START_BLOCK_TOKEN_MBW) {
				currentByteInSector++;
			}
			break;
		}
		if (currentByteInSector < SECTOR_SIZE) {
			sectorBuf[currentByteInSector] = value;
		}
		currentByteInSector++;
		if (currentByteInSector == (SECTOR_SIZE + 2)) {
			// check if still in valid range
			byte response = DRT_ACCEPTED;
			if (currentSector >= (ram->getSize() / SECTOR_SIZE)) {
				response = DRT_WRITE_ERROR;
				// note: mode is not changed, should be done by the host with CMD12 (STOP_TRANSMISSION)
			} else {
				// copy buffer to SD card
				for (unsigned bc = 0; bc < SECTOR_SIZE; bc++) {
					ram->write(currentSector * SECTOR_SIZE + bc, sectorBuf[bc]);
				}
				currentByteInSector = -1;
				currentSector++;
			}
			transferDelayCounter = 1;
			responseQueue.push_back(response);
		}
		break;
	case COMMAND:
	default:
		if ((cmdIdx == 0 && (value >> 6) == 1) // command start
				|| cmdIdx > 0) { // command in progress
			cmdBuf[cmdIdx] = value;
			++cmdIdx;
			if (cmdIdx == 6) {
				executeCommand();
				cmdIdx = 0;
			}
		}
		break;
	}
	
	return retval;
}

void SdCard::executeCommand()
{
	// it takes 2 transfers (2x8 cycles) before a reply
	// can be given to a command
	transferDelayCounter = 2;
	byte command = cmdBuf[0] & 0x3F;
	switch (command) {
	case 0:  // GO_IDLE_STATE
		reset();
		responseQueue.push_back(R1_IDLE);
		break;
	case 8:  // SEND_IF_COND
		// conditions are always OK
		responseQueue.push_back(R1_IDLE); // R1 (OK) SDHC
		responseQueue.push_back(0x02); // command version
		responseQueue.push_back(0x00); // reserved
		responseQueue.push_back(0x01); // voltage accepted
		responseQueue.push_back(cmdBuf[4]); // check pattern
		break;
	case 9:{ // SEND_CSD 
		responseQueue.push_back(R1_IDLE); // OK
		// now follows a CSD version 2.0 (for SDHC)
		responseQueue.push_back(START_BLOCK_TOKEN); // data token
		responseQueue.push_back(0x01); // CSD_STRUCTURE [127:120]
		responseQueue.push_back(0x0E); // (TAAC)
		responseQueue.push_back(0x00); // (NSAC) 
		responseQueue.push_back(0x32); // (TRAN_SPEED)
		responseQueue.push_back(0x00); // CCC
		responseQueue.push_back(0x00); // CCC / (READ_BL_LEN)
		responseQueue.push_back(0x00); // (RBP)/(WBM)/(RBM)/ DSR_IMP 
		// SD_CARD_SIZE = (C_SIZE + 1) * 512kByte
		unsigned c_size = ram->getSize() / (512 * 1024) - 1;
		responseQueue.push_back((c_size >> 16) & 0x3F); // C_SIZE 1
		responseQueue.push_back((c_size >>  8) & 0xFF); // C_SIZE 2
		responseQueue.push_back((c_size >>  0) & 0xFF); // C_SIZE 3
		responseQueue.push_back(0x00); // res/(EBE)/(SS1)
		responseQueue.push_back(0x00); // (SS2)/(WGS)
		responseQueue.push_back(0x00); // (WGE)/res/(RF)/(WBL1)
		responseQueue.push_back(0x00); // (WBL2)/(WBP)/res 
		responseQueue.push_back(0x00); // (FFG)/COPY/PWP/TWP/(FF)/res
		responseQueue.push_back(0x01); // CRC / 1
		break;}
	case 10: // SEND_CID
		responseQueue.push_back(R1_IDLE); // OK
		responseQueue.push_back(START_BLOCK_TOKEN); // data token
		responseQueue.push_back(0xAA); // CID01 // manuf ID
		responseQueue.push_back( 'o'); // CID02 // OEM/App ID 1
		responseQueue.push_back( 'p'); // CID03 // OEM/App ID 2
		responseQueue.push_back( 'e'); // CID04 // Prod name 1
		responseQueue.push_back( 'n'); // CID05 // Prod name 2
		responseQueue.push_back( 'M'); // CID06 // Prod name 3
		responseQueue.push_back( 'S'); // CID07 // Prod name 4
		responseQueue.push_back( 'X'); // CID08 // Prod name 5
		responseQueue.push_back(0x01); // CID09 // Prod Revision
		responseQueue.push_back(0x12); // CID10 // Prod Serial 1
		responseQueue.push_back(0x34); // CID11 // Prod Serial 2
		responseQueue.push_back(0x56); // CID12 // Prod Serial 3 
		responseQueue.push_back(0x78); // CID13 // Prod Serial 4
		responseQueue.push_back(0x00); // CID14 // reserved / Y1
		responseQueue.push_back(0xE6); // CID15 // Y2 / M
		responseQueue.push_back(0x01); // CID16 // CRC / not used
		break;
	case 12: // STOP TRANSMISSION
		responseQueue.push_back(R1_IDLE); // R1 (OK)
		mode = COMMAND;
		break;
	case 16: // SET_BLOCKLEN
		responseQueue.push_back(R1_IDLE); // OK, we don't really care
		break;
	case 17: // READ_SINGLE_BLOCK
	case 18: // READ_MULTIPLE_BLOCK
	case 24: // WRITE_BLOCK
	case 25: // WRITE_MULTIPLE_BLOCK
		// SDHC so the address is the sector
		currentSector = Endian::readB32(&cmdBuf[1]);
		if (currentSector >= (ram->getSize() / SECTOR_SIZE)) {
			responseQueue.push_back(R1_PARAMETER_ERROR);
		} else {
			responseQueue.push_back(R1_BUSY);
			switch (command) {
				case 17: mode = READ; break;
				case 18: mode = MULTI_READ; break;
				case 24: mode = WRITE; break;
				case 25: mode = MULTI_WRITE; break;
				default: UNREACHABLE;
			}
			currentByteInSector = -1; // wait for token
		}
		break;
	case 41: // implementation of ACMD 41!!
		 // SD_SEND_OP_COND
		responseQueue.push_back(R1_BUSY);
		break;
	case 55: // APP_CMD
		// TODO: go to ACMD mode, but not necessary now
		responseQueue.push_back(R1_IDLE);
		break;
	case 58: // READ_OCR
		responseQueue.push_back(0x01); // R1 (OK)
		responseQueue.push_back(0x40); // OCR Reg part 1 (SDHC: CCS=1)
		responseQueue.push_back(0x00); // OCR Reg part 2
		responseQueue.push_back(0x00); // OCR Reg part 3
		responseQueue.push_back(0x00); // OCR Reg part 4
		break;
	
	default:
		responseQueue.push_back(R1_ILLEGAL_COMMAND);
		break;
	}
}

} // namespace openmsx