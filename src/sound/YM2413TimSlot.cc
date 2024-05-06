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
{
}

void Slot::reset()
{
    pg_phase = 0;
    output = 0;
    feedback = 0;
    setEnvelopeState(FINISH);
    eg_tll = 0;
    sustain = false;
    eg_volume = TL2EG(0);
    slot_on_flag = 0;
    eg_rks = 0;
}

void Slot::updatePG(uint16_t freq)
{
    pg_freq = freq;
}

void Slot::updateTLL(uint16_t freq, bool actAsCarrier)
{
    eg_tll = patch.KL[freq >> 5] + (actAsCarrier ? eg_volume : patch.TL);
}

void Slot::updateRKS(uint16_t freq)
{
    uint16_t rks = freq >> patch.KR;
    assert(rks < 16);
    eg_rks = rks;
}

void Slot::updateEG()
{
    switch (eg_state) {
    case ATTACK:
        // Original code had separate table for AR, the values in
        // this table were 12 times bigger than the values in the
        // dPhaseDrTab[eg_rks] table, expect for the value for AR=15.
        // But when AR=15, the value doesn't matter.
        //
        // This factor 12 can also be seen in the attack/decay rates
        // table in the ym2413 application manual (table III-7, page
        // 13). For other chips like OPL1, OPL3 this ratio seems to be
        // different.
        eg_dPhase = dPhaseDrTab[eg_rks][patch.AR] * 12;
        break;
    case DECAY:
        eg_dPhase = dPhaseDrTab[eg_rks][patch.DR];
        break;
    case SUSTAIN:
        eg_dPhase = dPhaseDrTab[eg_rks][patch.RR];
        break;
    case RELEASE: {
        unsigned idx = sustain ? 5 : (patch.EG ? patch.RR : 7);
        eg_dPhase = dPhaseDrTab[eg_rks][idx];
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
        eg_dPhase = (dPhaseDrTab[eg_rks][12]);
        break;
    case SUSHOLD:
    case FINISH:
        eg_dPhase = 0;
        break;
    }
}

void Slot::updateAll(uint16_t freq, bool actAsCarrier)
{
    updatePG(freq);
    updateTLL(freq, actAsCarrier);
    updateRKS(freq);
    updateEG(); // EG should be updated last
}

void Slot::setEnvelopeState(EnvelopeState state)
{
    eg_state = state;
    switch (eg_state) {
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
    return eg_state != FINISH;
}

// Slot key on
void Slot::slotOn()
{
    setEnvelopeState(ATTACK);
    eg_phase = 0;
    pg_phase = 0;
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
    if (eg_state == FINISH) return; // already in off state
    if (eg_state == ATTACK) {
        eg_phase = (arAdjustTab[eg_phase >> EP_FP_BITS /*.toInt()*/]) << EP_FP_BITS;
    }
    setEnvelopeState(RELEASE);
}


// Change a rhythm voice
void Slot::setPatch(const Patch& newPatch)
{
    patch = newPatch; // copy data
    if ((eg_state == SUSHOLD) && (patch.EG == 0)) {
        setEnvelopeState(SUSTAIN);
    }
    setEnvelopeState(eg_state); // recalculate eg_phase_max
}

// Set new volume based on 4-bit register value (0-15).
void Slot::setVolume(unsigned value)
{
    // '<< 2' to transform 4 bits to the same range as the 6 bits TL field
    eg_volume = TL2EG(value << 2);
}

// PG
unsigned Slot::calc_phase(unsigned lfo_pm)
{
    uint16_t fnum = pg_freq & 511;
    uint16_t block = pg_freq / 512;
    unsigned tmp = ((2 * fnum + pmTable[fnum >> 6][lfo_pm]) * patch.ML) << block;
    uint16_t dphase = tmp >> (21 - DP_BITS);

    pg_phase += dphase;
    return pg_phase >> DP_BASE_BITS;
}

// EG
void Slot::calc_envelope_outline(unsigned& out)
{
    switch (eg_state) {
    case ATTACK:
        out = 0;
        eg_phase = 0;
        setEnvelopeState(DECAY);
        break;
    case DECAY:
        eg_phase = eg_phase_max;
        setEnvelopeState(patch.EG ? SUSHOLD : SUSTAIN);
        break;
    case SUSTAIN:
    case RELEASE:
        setEnvelopeState(FINISH);
        break;
    case SETTLE:
        // Comment copied from Burczynski code (didn't verify myself):
        //   When CARRIER envelope gets down to zero level, phases in
        //   BOTH operators are reset (at the same time?).
        slotOn();
        sibling->slotOn();
        break;
    case SUSHOLD:
    case FINISH:
    default:
        UNREACHABLE;
    }
}

unsigned Slot::calc_envelope(bool HAS_AM, bool FIXED_ENV, int lfo_am, unsigned fixed_env)
{
    assert(!FIXED_ENV || (eg_state == one_of(SUSHOLD, FINISH)));

    if (FIXED_ENV) {
        unsigned out = fixed_env;
        if (HAS_AM) {
            out += lfo_am; // [0, 768)
            out |= 3;
        }
        else {
            // out |= 3   is already done in calc_fixed_env()
        }
        return out;
    }
    else {
        unsigned out = eg_phase >> EP_FP_BITS; // .toInt(); // in range [0, 128]
        if (eg_state == ATTACK) {
            out = arAdjustTab[out]; // [0, 128]
        }
        eg_phase += eg_dPhase;
        if (eg_phase >= eg_phase_max) {
            calc_envelope_outline(out);
        }
        out = EG2DB(out + eg_tll); // [0, 732]
        if (HAS_AM) {
            out += lfo_am; // [0, 758]
        }
        return out | 3;
    }
}

unsigned Slot::calc_fixed_env(bool HAS_AM) const
{
    assert(eg_state == one_of(SUSHOLD, FINISH));
    assert(eg_dPhase == 0);
    unsigned out = eg_phase >> EP_FP_BITS; // .toInt(); // in range [0, 128)
    out = EG2DB(out + eg_tll); // [0, 480)
    if (!HAS_AM) {
        out |= 3;
    }
    return out;
}

// Convert Amp(0 to EG_HEIGHT) to Phase(0 to 8PI)
static constexpr int wave2_8pi(int e)
{
    int shift = SLOT_AMP_BITS - PG_BITS - 2;
    if (shift > 0) {
        return e >> shift;
    }
    else {
        return e << -shift;
    }
}

// CARRIER
int Slot::calc_slot_car(bool HAS_AM, bool FIXED_ENV, unsigned lfo_pm, int lfo_am, int fm, unsigned fixed_env)
{
    int phase = narrow<int>(calc_phase(lfo_pm)) + wave2_8pi(fm);
    unsigned egOut = calc_envelope(HAS_AM, FIXED_ENV, lfo_am, fixed_env);
    int newOutput = dB2LinTab[patch.WF[phase & PG_MASK] + egOut];
    output = (output + newOutput) >> 1;
    return output;
}

// MODULATOR
int Slot::calc_slot_mod(bool HAS_AM, bool HAS_FB, bool FIXED_ENV, unsigned lfo_pm, int lfo_am, unsigned fixed_env)
{
    assert((patch.FB != 0) == HAS_FB);
    unsigned phase = calc_phase(lfo_pm);
    unsigned egOut = calc_envelope(HAS_AM, FIXED_ENV, lfo_am, fixed_env);
    if (HAS_FB) {
        phase += wave2_8pi(feedback) >> patch.FB;
    }
    int newOutput = dB2LinTab[patch.WF[phase & PG_MASK] + egOut];
    feedback = (output + newOutput) >> 1;
    output = newOutput;
    return feedback;
}

// TOM (ch8 mod)
int Slot::calc_slot_tom()
{
    unsigned phase = calc_phase(0);
    unsigned egOut = calc_envelope(false, false, 0, 0);
    return dB2LinTab[patch.WF[phase & PG_MASK] + egOut];
}

// SNARE (ch7 car)
int Slot::calc_slot_snare(bool noise)
{
    unsigned phase = calc_phase(0);
    unsigned egOut = calc_envelope(false, false, 0, 0);
    return BIT(phase, 7)
        ? dB2LinTab[(noise ? DB_POS(0.0) : DB_POS(15.0)) + egOut]
        : dB2LinTab[(noise ? DB_NEG(0.0) : DB_NEG(15.0)) + egOut];
}

// TOP-CYM (ch8 car)
int Slot::calc_slot_cym(unsigned phase7, unsigned phase8)
{
    unsigned egOut = calc_envelope(false, false, 0, 0);
    unsigned dbOut = (((BIT(phase7, PG_BITS - 8) ^
        BIT(phase7, PG_BITS - 1)) |
        BIT(phase7, PG_BITS - 7)) ^
        (BIT(phase8, PG_BITS - 7) &
            !BIT(phase8, PG_BITS - 5)))
        ? DB_NEG(3.0)
        : DB_POS(3.0);
    return dB2LinTab[dbOut + egOut];
}

// HI-HAT (ch7 mod)
int Slot::calc_slot_hat(unsigned phase7, unsigned phase8, bool noise)
{
    unsigned egOut = calc_envelope(false, false, 0, 0);
    unsigned dbOut = (((BIT(phase7, PG_BITS - 8) ^
        BIT(phase7, PG_BITS - 1)) |
        BIT(phase7, PG_BITS - 7)) ^
        (BIT(phase8, PG_BITS - 7) &
            !BIT(phase8, PG_BITS - 5)))
        ? (noise ? DB_NEG(12.0) : DB_NEG(24.0))
        : (noise ? DB_POS(12.0) : DB_POS(24.0));
    return dB2LinTab[dbOut + egOut];
}

} // namespace YM2413Tim
} // namespace openmsx
