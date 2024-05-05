/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#pragma once

#include "YM2413TimPatch.hh"

namespace openmsx {
namespace YM2413Tim {

//
// Slot
//
class Slot {
public:
    Slot();
    void reset();

    void setEnvelopeState(EnvelopeState state);
    bool isActive() const;

    void slotOn();
    void slotOn2();
    void slotOff();
    void setPatch(const Patch& patch);
    void setVolume(unsigned value);

    unsigned calc_phase(unsigned lfo_pm);
    unsigned calc_envelope(bool HAS_AM, bool FIXED_ENV, int lfo_am, unsigned fixed_env);
    unsigned calc_fixed_env(bool HAS_AM) const;
    void calc_envelope_outline(unsigned& out);
    int calc_slot_car(bool HAS_AM, bool FIXED_ENV, unsigned lfo_pm, int lfo_am, int fm, unsigned fixed_env);
    int calc_slot_mod(bool HAS_AM, bool HAS_FB, bool FIXED_ENV, unsigned lfo_pm, int lfo_am, unsigned fixed_env);

    int calc_slot_tom();
    int calc_slot_snare(bool noise);
    int calc_slot_cym(unsigned phase7, unsigned phase8);
    int calc_slot_hat(unsigned phase7, unsigned phase8, bool noise);
    void updatePG(uint16_t freq);
    void updateTLL(uint16_t freq, bool actAsCarrier);
    void updateRKS(uint16_t freq);
    void updateEG();
    void updateAll(uint16_t freq, bool actAsCarrier);

    template<typename Archive>
    void serialize(Archive& ar, unsigned version);

    // OUTPUT
    int feedback;
    int output;     // Output value of slot

    // for Phase Generator (PG)
    unsigned cPhase;        // Phase counter
    std::array<uint16_t, 8> dPhase; // Phase increment

    // for Envelope Generator (EG)
    unsigned volume;             // Current volume
    unsigned tll;                // Total Level + Key scale level
    std::span<const int, 16> dPhaseDRTableRks; // (converted to EnvPhaseIndex)
    EnvelopeState state;         // Current state
    EnvPhaseIndex eg_phase;      // Phase
    EnvPhaseIndex eg_dPhase;     // Phase increment amount
    EnvPhaseIndex eg_phase_max;
    uint8_t slot_on_flag;
    bool sustain;                // Sustain

    Patch patch;
    Slot* sibling; // pointer to sibling slot (only valid for car -> mod)
};

} // namespace YM2413Tim
} // namespace openmsx
