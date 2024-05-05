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

Channel::Channel()
{
    car.sibling = &mod;    // car needs a pointer to its sibling
    mod.sibling = nullptr; // mod doesn't need this pointer
}

void Channel::reset()
{
    mod.reset();
    car.reset();
}

// Change a voice
void Channel::setPatch(const Patch& modPatch, const Patch& carPatch)
{
    mod.setPatch(modPatch);
    car.setPatch(carPatch);
}

// Set sustain parameter
void Channel::setSustain(bool sustain, bool modActAsCarrier)
{
    car.sustain = sustain;
    if (modActAsCarrier) {
        mod.sustain = sustain;
    }
}

// Channel key on
void Channel::keyOn()
{
    // TODO Should we also test mod.slot_on_flag?
    //      Should we    set    mod.slot_on_flag?
    //      Can make a difference for channel 7/8 in rythm mode.
    if (!car.slot_on_flag) {
        car.setEnvelopeState(SETTLE);
        // this will shortly set both car and mod to ATTACK state
    }
    car.slot_on_flag |= 1;
    mod.slot_on_flag |= 1;
}

// Channel key off
void Channel::keyOff()
{
    // Note: no mod.slotOff() in original code!!!
    if (car.slot_on_flag) {
        car.slot_on_flag &= ~1;
        mod.slot_on_flag &= ~1;
        if (!car.slot_on_flag) {
            car.slotOff();
        }
    }
}

} // namespace YM2413Tim
} // namespace openmsx
