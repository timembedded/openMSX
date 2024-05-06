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

Slot Slot::_instance;

//
// Slot
//

Slot::Slot() :
    patch(Patch::instance())
{
}

void Slot::reset()
{
    sd->pg_phase = 0;
    sd->output = 0;
    sd->feedback = 0;
    setEnvelopeState(FINISH);
    sd->eg_tll = 0;
    sd->sustain = false;
    sd->eg_volume = TL2EG(0);
    sd->slot_on_flag = 0;
    sd->eg_rks = 0;
}

void Slot::select(int num)
{
    sd = &slotData[num];
    patch.select(sd->patch);
}

void Slot::updatePG(uint16_t freq)
{
    sd->pg_freq = freq;
}

void Slot::updateTLL(uint16_t freq, bool actAsCarrier)
{
    sd->eg_tll = patch.pd->KL[freq >> 5] + (actAsCarrier ? sd->eg_volume : patch.pd->TL);
}

void Slot::updateRKS(uint16_t freq)
{
    uint16_t rks = freq >> patch.pd->KR;
    assert(rks < 16);
    sd->eg_rks = rks;
}

void Slot::updateEG()
{
    switch (sd->eg_state) {
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
        sd->eg_dPhase = dPhaseDrTab[sd->eg_rks][patch.pd->AR] * 12;
        break;
    case DECAY:
        sd->eg_dPhase = dPhaseDrTab[sd->eg_rks][patch.pd->DR];
        break;
    case SUSTAIN:
        sd->eg_dPhase = dPhaseDrTab[sd->eg_rks][patch.pd->RR];
        break;
    case RELEASE: {
        unsigned idx = sd->sustain ? 5 : (patch.pd->EG ? patch.pd->RR : 7);
        sd->eg_dPhase = dPhaseDrTab[sd->eg_rks][idx];
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
        sd->eg_dPhase = (dPhaseDrTab[sd->eg_rks][12]);
        break;
    case SUSHOLD:
    case FINISH:
        sd->eg_dPhase = 0;
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
    sd->eg_state = state;
    switch (sd->eg_state) {
    case ATTACK:
        sd->eg_phase_max = (patch.pd->AR == 15) ? 0 : EG_DP_MAX;
        break;
    case DECAY:
        sd->eg_phase_max = narrow<int>(patch.pd->SL);
        break;
    case SUSHOLD:
        sd->eg_phase_max = EG_DP_INF;
        break;
    case SETTLE:
    case SUSTAIN:
    case RELEASE:
        sd->eg_phase_max = EG_DP_MAX;
        break;
    case FINISH:
        sd->eg_phase = EG_DP_MAX;
        sd->eg_phase_max = EG_DP_INF;
        break;
    }
    updateEG();
}

bool Slot::isActive() const
{
    return sd->eg_state != FINISH;
}

// Slot key on
void Slot::slotOn()
{
    setEnvelopeState(ATTACK);
    sd->eg_phase = 0;
    sd->pg_phase = 0;
}

// Slot key on, without resetting the phase
void Slot::slotOn2()
{
    setEnvelopeState(ATTACK);
    sd->eg_phase = 0;
}

// Slot key off
void Slot::slotOff()
{
    if (sd->eg_state == FINISH) return; // already in off state
    if (sd->eg_state == ATTACK) {
        sd->eg_phase = (arAdjustTab[sd->eg_phase >> EP_FP_BITS /*.toInt()*/]) << EP_FP_BITS;
    }
    setEnvelopeState(RELEASE);
}


// Change a rhythm voice
void Slot::setPatch(int voice)
{
    sd->patch = voice;
    patch.select(sd->patch);
    if ((sd->eg_state == SUSHOLD) && (patch.pd->EG == 0)) {
        setEnvelopeState(SUSTAIN);
    }
    setEnvelopeState(sd->eg_state); // recalculate eg_phase_max
}

// Set new volume based on 4-bit register value (0-15).
void Slot::setVolume(unsigned value)
{
    // '<< 2' to transform 4 bits to the same range as the 6 bits TL field
    sd->eg_volume = TL2EG(value << 2);
}

// PG
unsigned Slot::calc_phase(unsigned lfo_pm)
{
    uint16_t fnum = sd->pg_freq & 511;
    uint16_t block = sd->pg_freq / 512;
    unsigned tmp = ((2 * fnum + pmTable[fnum >> 6][lfo_pm]) * patch.pd->ML) << block;
    uint16_t dphase = tmp >> (21 - DP_BITS);

    sd->pg_phase += dphase;
    return sd->pg_phase >> DP_BASE_BITS;
}

// EG
void Slot::calc_envelope_outline(unsigned& out)
{
    switch (sd->eg_state) {
    case ATTACK:
        out = 0;
        sd->eg_phase = 0;
        setEnvelopeState(DECAY);
        break;
    case DECAY:
        sd->eg_phase = sd->eg_phase_max;
        setEnvelopeState(patch.pd->EG ? SUSHOLD : SUSTAIN);
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
        assert (sd->sibling != -1);
        if (sd->sibling != -1) {
            SlotData *sdsave = sd;
            select(sd->sibling);
            slotOn();
            sd = sdsave;
        }
        break;
    case SUSHOLD:
    case FINISH:
    default:
        UNREACHABLE;
    }
}

unsigned Slot::calc_envelope(bool HAS_AM, bool FIXED_ENV, int lfo_am, unsigned fixed_env)
{
    assert(!FIXED_ENV || (sd->eg_state == one_of(SUSHOLD, FINISH)));

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
        unsigned out = sd->eg_phase >> EP_FP_BITS; // .toInt(); // in range [0, 128]
        if (sd->eg_state == ATTACK) {
            out = arAdjustTab[out]; // [0, 128]
        }
        sd->eg_phase += sd->eg_dPhase;
        if (sd->eg_phase >= sd->eg_phase_max) {
            calc_envelope_outline(out);
        }
        out = EG2DB(out + sd->eg_tll); // [0, 732]
        if (HAS_AM) {
            out += lfo_am; // [0, 758]
        }
        return out | 3;
    }
}

unsigned Slot::calc_fixed_env(bool HAS_AM) const
{
    assert(sd->eg_state == one_of(SUSHOLD, FINISH));
    assert(sd->eg_dPhase == 0);
    unsigned out = sd->eg_phase >> EP_FP_BITS; // .toInt(); // in range [0, 128)
    out = EG2DB(out + sd->eg_tll); // [0, 480)
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
    int newOutput = dB2LinTab[patch.pd->WF[phase & PG_MASK] + egOut];
    sd->output = (sd->output + newOutput) >> 1;
    return sd->output;
}

// MODULATOR
int Slot::calc_slot_mod(bool HAS_AM, bool HAS_FB, bool FIXED_ENV, unsigned lfo_pm, int lfo_am, unsigned fixed_env)
{
    assert((patch.pd->FB != 0) == HAS_FB);
    unsigned phase = calc_phase(lfo_pm);
    unsigned egOut = calc_envelope(HAS_AM, FIXED_ENV, lfo_am, fixed_env);
    if (HAS_FB) {
        phase += wave2_8pi(sd->feedback) >> patch.pd->FB;
    }
    int newOutput = dB2LinTab[patch.pd->WF[phase & PG_MASK] + egOut];
    sd->feedback = (sd->output + newOutput) >> 1;
    sd->output = newOutput;
    return sd->feedback;
}

// TOM (ch8 mod)
int Slot::calc_slot_tom()
{
    unsigned phase = calc_phase(0);
    unsigned egOut = calc_envelope(false, false, 0, 0);
    return dB2LinTab[patch.pd->WF[phase & PG_MASK] + egOut];
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
