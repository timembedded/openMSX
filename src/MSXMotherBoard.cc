// $Id$

#include "MSXMotherBoard.hh"
#include "RealTime.hh"
#include "DummyDevice.hh"
#include "Leds.hh"
#include "CPU.hh"
#include "MSXCPU.hh"
#include "MSXDevice.hh"
#include "MSXIODevice.hh"
#include "MSXMemDevice.hh"
#include "MSXException.hh"
#include "Console.hh"

#include <cassert>
#include <sstream>

MSXMotherBoard::MSXMotherBoard()
{
	PRT_DEBUG("Creating an MSXMotherBoard object");

	DummyDevice* dummy = DummyDevice::instance();
	for (int port=0; port<256; port++) {
		IO_In [port] = dummy;
		IO_Out[port] = dummy;
	}
	for (int primSlot=0; primSlot<4; primSlot++) {
		for (int secSlot=0; secSlot<4; secSlot++) {
			for (int page=0; page<4; page++) {
				SlotLayout[primSlot][secSlot][page]=dummy;
			}
		}
	}
	for (int primSlot=0; primSlot<4; primSlot++) {
		isSubSlotted[primSlot] = false;
	}

	config = MSXConfig::Backend::instance()->getConfigById("MotherBoard");
	std::list<MSXConfig::Device::Parameter*>* subslotted_list;
	subslotted_list = config->getParametersWithClass("subslotted");
	std::list<MSXConfig::Device::Parameter*>::const_iterator i;
	for (i=subslotted_list->begin(); i != subslotted_list->end(); i++) {
		bool hasSubs=false;
		if ((*i)->value == "true") {
			hasSubs=true;
		}
		int counter=atoi((*i)->name.c_str());
		isSubSlotted[counter]=hasSubs;
		PRT_DEBUG("Slot: " << counter << " expanded: " << hasSubs);
	}
	config->getParametersWithClassClean(subslotted_list);

	// Register console commands.
	Console::instance()->registerCommand(this, "slotmap");

}

MSXMotherBoard::~MSXMotherBoard()
{
	PRT_DEBUG("Destructing an MSXMotherBoard object");
}

MSXMotherBoard *MSXMotherBoard::instance()
{
	if (oneInstance == NULL) {
		oneInstance = new MSXMotherBoard();
	}
	return oneInstance;
}
MSXMotherBoard *MSXMotherBoard::oneInstance = NULL;


void MSXMotherBoard::register_IO_In(byte port, MSXIODevice *device)
{
	if (IO_In[port] == DummyDevice::instance()) {
		PRT_DEBUG (device->getName() << " registers In-port " <<std::hex<< (int)port<<std::dec);
		IO_In[port] = device;
	} else {
		PRT_ERROR (device->getName() << " trying to register taken In-port "
		                        <<std::hex << (int)port << std::dec);
	}
}

void MSXMotherBoard::register_IO_Out(byte port, MSXIODevice *device)
{
	if (IO_Out[port] == DummyDevice::instance()) {
		PRT_DEBUG (device->getName() << " registers Out-port " <<std::hex<<(int)port<<std::dec);
		IO_Out[port] = device;
	} else {
		PRT_ERROR (device->getName() << " trying to register taken Out-port "
		                        <<std::hex<< (int)port<<std::dec);
	}
}

void MSXMotherBoard::addDevice(MSXDevice *device)
{
	availableDevices.push_back(device);
}

void MSXMotherBoard::registerSlottedDevice(MSXMemDevice *device, int primSl, int secSl, int page)
{
	if (!isSubSlotted[primSl] && secSl != 0) {
		std::ostringstream s;
		s << "slot " << primSl << "." << secSl
			<< " does not exist, because slot is not expanded";
		throw MSXException(s.str());
	}
	if (SlotLayout[primSl][secSl][page] == DummyDevice::instance()) {
		PRT_DEBUG(device->getName() << " registers at "<<primSl<<" "<<secSl<<" "<<page);
		SlotLayout[primSl][secSl][page] = device;
	} else {
		PRT_ERROR(device->getName() << " trying to register taken slot");
	}
}

void MSXMotherBoard::ResetMSX(const EmuTime &time)
{
	IRQLine = 0;
	set_A8_Register(0);
	std::vector<MSXDevice*>::iterator i;
	for (i = availableDevices.begin(); i != availableDevices.end(); i++) {
		(*i)->reset(time);
	}
}

void MSXMotherBoard::StartMSX()
{
	IRQLine = 0;
	set_A8_Register(0);
	Leds::instance()->setLed(Leds::POWER_ON);
	RealTime::instance();
	Scheduler::instance()->scheduleEmulation();
}

void MSXMotherBoard::DestroyMSX()
{
	std::vector<MSXDevice*>::iterator i;
	for (i = availableDevices.begin(); i != availableDevices.end(); i++) {
		delete (*i);
	}
}

void MSXMotherBoard::SaveStateMSX(std::ofstream &savestream)
{
	std::vector<MSXDevice*>::iterator i;
	for (i = availableDevices.begin(); i != availableDevices.end(); i++) {
		(*i)->saveState(savestream);
	}
}


void MSXMotherBoard::set_A8_Register(byte value)
{
	for (int page=0; page<4; page++, value>>=2) {
		// Change the slot structure
		PrimarySlotState[page] = value&3;
		SecondarySlotState[page] = 3&(SubSlot_Register[value&3]>>(page*2));
		// Change the visible devices
		MSXMemDevice* newDevice = SlotLayout [PrimarySlotState[page]]
		                                     [SecondarySlotState[page]]
		                                     [page];
		if (visibleDevices[page] != newDevice) {
			visibleDevices[page] = newDevice;
			// invalidate cache
			MSXCPU::instance()->invalidateCache(page*0x4000, 0x4000/CPU::CACHE_LINE_SIZE);
		}
	}
}


// CPU Interface //

byte MSXMotherBoard::readMem(word address, const EmuTime &time)
{
	if (address == 0xFFFF) {
		int CurrentSSRegister = PrimarySlotState[3];
		if (isSubSlotted[CurrentSSRegister]) {
			return 255^SubSlot_Register[CurrentSSRegister];
		}
	}
	return visibleDevices[address>>14]->readMem(address, time);
}

void MSXMotherBoard::writeMem(word address, byte value, const EmuTime &time)
{
	if (address == 0xFFFF) {
		int CurrentSSRegister = PrimarySlotState[3];
		if (isSubSlotted[CurrentSSRegister]) {
			SubSlot_Register[CurrentSSRegister] = value;
			for (int page=0; page<4; page++, value>>=2) {
				if (CurrentSSRegister == PrimarySlotState[page]) {
					SecondarySlotState[page] = value&3;
					// Change the visible devices
					MSXMemDevice* newDevice = SlotLayout [PrimarySlotState[page]]
					                                     [SecondarySlotState[page]]
					                                     [page];
					if (visibleDevices[page] != newDevice) {
						visibleDevices[page] = newDevice;
						// invalidate cache
						MSXCPU::instance()->invalidateCache(page*0x4000, 0x4000/CPU::CACHE_LINE_SIZE);
					}
				}
			}
			return;
		}
	}
	// address is not FFFF or it is but there is no subslotregister visible
	visibleDevices[address>>14]->writeMem(address, value, time);
}

byte MSXMotherBoard::readIO(word prt, const EmuTime &time)
{
	byte port = (byte)prt;
	return IO_In[port]->readIO(port, time);
}

void MSXMotherBoard::writeIO(word prt, byte value, const EmuTime &time)
{
	byte port = (byte)prt;
	IO_Out[port]->writeIO(port, value, time);
}


bool MSXMotherBoard::IRQStatus()
{
	return (bool)IRQLine;
}

void MSXMotherBoard::raiseIRQ()
{
	IRQLine++;
}

void MSXMotherBoard::lowerIRQ()
{
	assert (IRQLine != 0);
	IRQLine--;
}

byte* MSXMotherBoard::getReadCacheLine(word start)
{
	if ((start == 0x10000-CPU::CACHE_LINE_SIZE) &&	// contains 0xffff
	    (isSubSlotted[PrimarySlotState[3]]))
		return NULL;
	return visibleDevices[start>>14]->getReadCacheLine(start);
}

byte* MSXMotherBoard::getWriteCacheLine(word start)
{
	if ((start == 0x10000-CPU::CACHE_LINE_SIZE) &&	// contains 0xffff
	    (isSubSlotted[PrimarySlotState[3]]))
		return NULL;
	return visibleDevices[start>>14]->getWriteCacheLine(start);
}

std::string MSXMotherBoard::getSlotMap()
{
	std::ostringstream out;
	for (int prim = 0; prim < 4; prim++) {
		if (isSubSlotted[prim]) {
			for (int sec = 0; sec < 4; sec++) {
				out << "slot " << prim << "." << sec << ":\n";
				printSlotMapPages(out, SlotLayout[prim][sec]);
			}
		} else {
			out << "slot " << prim << ":\n";
			printSlotMapPages(out, SlotLayout[prim][0]);
		}
	}
	return out.str();
}

void MSXMotherBoard::printSlotMapPages(
	std::ostream &out, MSXMemDevice *devices[])
{
	for (int page = 0; page < 4; page++) {
		char hexStr[5];
		snprintf(hexStr, sizeof(hexStr), "%04X", page * 0x4000);
		out << hexStr << ": " << devices[page]->getName() << "\n";
	}
}

void MSXMotherBoard::ConsoleCallback(char *commandLine)
{
	assert(strncmp(commandLine, "slotmap", 7) == 0);
	//cout << getSlotMap();
	Console::instance()->printOnConsole(getSlotMap());
}

void MSXMotherBoard::ConsoleHelp(char *commandLine)
{
	assert(strncmp(commandLine, "slotmap", 7) == 0);
	Console::instance()->printOnConsole(
		"Prints which slots contain which devices.");
}

