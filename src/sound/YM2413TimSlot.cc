/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#include "YM2413TimSlot.hh"
#include "YM2413TimCommon.hh"
#include "Math.hh"
#include "cstd.hh"
#include "enumerate.hh"
#include "inline.hh"
#include "narrow.hh"
#include "one_of.hh"
#include "ranges.hh"
#include "serialize.hh"
#include "unreachable.hh"
#include "xrange.hh"
#include <array>
#include <cassert>
#include <iostream>

namespace openmsx {
namespace YM2413Tim {
    //
    // Slot
    //

    Slot::Slot()
        : dPhaseDRTableRks(dPhaseDrTab[0])
    {
    }

    void Slot::reset()
    {
        cPhase = 0;
        ranges::fill(dPhase, 0);
        output = 0;
        feedback = 0;
        setEnvelopeState(FINISH);
        dPhaseDRTableRks = dPhaseDrTab[0];
        tll = 0;
        sustain = false;
        volume = TL2EG(0);
        slot_on_flag = 0;
    }

    void Slot::updatePG(unsigned freq)
    {
        // Pre-calculate all phase-increments. The 8 different values are for
        // the 8 steps of the PM stuff (for mod and car phase calculation).
        // When PM isn't used then dPhase[0] is used (pmTable[.][0] == 0).
        // The original Okazaki core calculated the PM stuff in a different
        // way. This algorithm was copied from the Burczynski core because it
        // is much more suited for a (cheap) hardware calculation.
        unsigned fnum = freq & 511;
        unsigned block = freq / 512;
        for (auto [pm, dP] : enumerate(dPhase)) {
            unsigned tmp = ((2 * fnum + pmTable[fnum >> 6][pm]) * patch.ML) << block;
            dP = tmp >> (21 - DP_BITS);
        }
    }

    void Slot::updateTLL(unsigned freq, bool actAsCarrier)
    {
        tll = patch.KL[freq >> 5] + (actAsCarrier ? volume : patch.TL);
    }

    void Slot::updateRKS(unsigned freq)
    {
        unsigned rks = freq >> patch.KR;
        assert(rks < 16);
        dPhaseDRTableRks = dPhaseDrTab[rks];
    }

    void Slot::updateEG()
    {
        switch (state) {
        case ATTACK:
            // Original code had separate table for AR, the values in
            // this table were 12 times bigger than the values in the
            // dPhaseDRTableRks table, expect for the value for AR=15.
            // But when AR=15, the value doesn't matter.
            //
            // This factor 12 can also be seen in the attack/decay rates
            // table in the ym2413 application manual (table III-7, page
            // 13). For other chips like OPL1, OPL3 this ratio seems to be
            // different.
            eg_dPhase = dPhaseDRTableRks[patch.AR] * 12;
            break;
        case DECAY:
            eg_dPhase = dPhaseDRTableRks[patch.DR];
            break;
        case SUSTAIN:
            eg_dPhase = dPhaseDRTableRks[patch.RR];
            break;
        case RELEASE: {
            unsigned idx = sustain ? 5
                : (patch.EG ? patch.RR
                    : 7);
            eg_dPhase = dPhaseDRTableRks[idx];
            break;
        }
        case SETTLE:
            // Value based on ym2413 application manual:
            //  - p10: (envelope graph)
            //         DP: 10ms
            //  - p13: (table III-7 attack and decay rates)
            //         Rate 12-0 -> 10.22ms
            //         Rate 12-1 ->  8.21ms
            //         Rate 12-2 ->  6.84ms
            //         Rate 12-3 ->  5.87ms
            // The datasheet doesn't say anything about key-scaling for
            // this state (in fact it doesn't say much at all about this
            // state). Experiments showed that the with key-scaling the
            // output matches closer the real HW. Also all other states use
            // key-scaling.
            eg_dPhase = (dPhaseDRTableRks[12]);
            break;
        case SUSHOLD:
        case FINISH:
            eg_dPhase = 0;
            break;
        }
    }

    void Slot::updateAll(unsigned freq, bool actAsCarrier)
    {
        updatePG(freq);
        updateTLL(freq, actAsCarrier);
        updateRKS(freq);
        updateEG(); // EG should be updated last
    }

    void Slot::setEnvelopeState(EnvelopeState state_)
    {
        state = state_;
        switch (state) {
        case ATTACK:
            eg_phase_max = (patch.AR == 15) ? 0 : EG_DP_MAX;
            break;
        case DECAY:
            eg_phase_max = narrow<int>(patch.SL);
            break;
        case SUSHOLD:
            eg_phase_max = EG_DP_INF;
            break;
        case SETTLE:
        case SUSTAIN:
        case RELEASE:
            eg_phase_max = EG_DP_MAX;
            break;
        case FINISH:
            eg_phase = EG_DP_MAX;
            eg_phase_max = EG_DP_INF;
            break;
        }
        updateEG();
    }

    bool Slot::isActive() const
    {
        return state != FINISH;
    }

    // Slot key on
    void Slot::slotOn()
    {
        setEnvelopeState(ATTACK);
        eg_phase = 0;
        cPhase = 0;
    }

    // Slot key on, without resetting the phase
    void Slot::slotOn2()
    {
        setEnvelopeState(ATTACK);
        eg_phase = 0;
    }

    // Slot key off
    void Slot::slotOff()
    {
        if (state == FINISH) return; // already in off state
        if (state == ATTACK) {
            eg_phase = (arAdjustTab[eg_phase >> EP_FP_BITS /*.toInt()*/]) << EP_FP_BITS;
        }
        setEnvelopeState(RELEASE);
    }


    // Change a rhythm voice
    void Slot::setPatch(const Patch& newPatch)
    {
        patch = newPatch; // copy data
        if ((state == SUSHOLD) && (patch.EG == 0)) {
            setEnvelopeState(SUSTAIN);
        }
        setEnvelopeState(state); // recalculate eg_phase_max
    }

    // Set new volume based on 4-bit register value (0-15).
    void Slot::setVolume(unsigned value)
    {
        // '<< 2' to transform 4 bits to the same range as the 6 bits TL field
        volume = TL2EG(value << 2);
    }

} // namespace YM2413Tim
} // namespace openmsx
