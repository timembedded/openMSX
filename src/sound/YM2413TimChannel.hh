/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */
#pragma once

#include "YM2413TimSlot.hh"

namespace openmsx {
namespace YM2413Tim {

    class Channel {
    public:
        Channel();
        void reset();
        void setPatch(int modPatch, int carPatch);
        void setSustain(bool sustain, bool modActAsCarrier);
        void keyOn();
        void keyOff();

        int mod, car;
        Slot &slot;

        template<typename Archive>
        void serialize(Archive& ar, unsigned version);
    };

} // namespace YM2413Tim
} // namespace openmsx
