// $Id$
//
// MSXRom Patch hook: an object with a method that is called
// when EDFE is encountered by the CPU, for a certain address
//

#ifndef __MSXROMPATCHINTERFACE_HH__
#define __MSXROMPATCHINTERFACE_HH__

#include <list>

class MSXRomPatchInterface
{
	public:

		/**
		 * called by CPU on encountering an EDFE
		 */
		virtual void patch(int address)=0;

		/**
		 * list of addresses I am interested in
		 */
		std::list<int> &addresses() { return addr_list; }
	
	protected:

		/**
		 * it's up to the actual patch classes to provide
		 * this list of addresses
		 */
		std::list<int> addr_list;
};

#endif // __MSXROMPATCHINTERFACE_HH__
