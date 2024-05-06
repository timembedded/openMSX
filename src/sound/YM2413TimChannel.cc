/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#include "YM2413TimChannel.hh"
#include "YM2413TimCommon.hh"

namespace openmsx {
namespace YM2413Tim {

//
// Channel
//

Channel::Channel() :
    slot(Slot::instance())
{
}

void Channel::reset()
{
    slot.select(car);
    slot.reset();
    slot.sd->sibling = mod;    // car needs a pointer to its sibling
    slot.select(mod);
    slot.reset();
    slot.sd->sibling = -1;     // mod doesn't need this pointer
}

// Change a voice
void Channel::setPatch(int modPatch, int carPatch)
{
    slot.select(mod);
    slot.setPatch(modPatch);
    slot.select(car);
    slot.setPatch(carPatch);
}

// Set sustain parameter
void Channel::setSustain(bool sustain, bool modActAsCarrier)
{
    slot.select(car);
    slot.sd->sustain = sustain;
    if (modActAsCarrier) {
        slot.select(mod);
        slot.sd->sustain = sustain;
    }
}

// Channel key on
void Channel::keyOn()
{
    // TODO Should we also test mod.slot_on_flag?
    //      Should we    set    mod.slot_on_flag?
    //      Can make a difference for channel 7/8 in rythm mode.
    slot.select(car);
    if (!slot.sd->slot_on_flag) {
        slot.setEnvelopeState(SETTLE);
        // this will shortly set both car and mod to ATTACK state
    }
    slot.sd->slot_on_flag |= 1;
    slot.select(mod);
    slot.sd->slot_on_flag |= 1;
}

// Channel key off
void Channel::keyOff()
{
    // Note: no mod.slotOff() in original code!!!
    slot.select(car);
    if (slot.sd->slot_on_flag) {
        slot.sd->slot_on_flag &= ~1;
        slot.select(mod);
        slot.sd->slot_on_flag &= ~1;
        slot.select(car);
        if (!slot.sd->slot_on_flag) {
            slot.slotOff();
        }
    }
}

} // namespace YM2413Tim
} // namespace openmsx
