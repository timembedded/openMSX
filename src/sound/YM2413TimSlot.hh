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
protected:
    Slot();
    static Slot _instance;

public:
    void operator=(const Slot &) = delete;
    static Slot& instance() { return _instance; }

    void reset();

    void select(int num);
    void setEnvelopeState(EnvelopeState state);
    bool isActive() const;

    void slotOnVoice(bool settle);
    void slotOnRhythm(bool attack, bool settle, bool reset_phase);
    void slotOffRhythm();
    void slotOffVoice();

    void setPatch(int voice);
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

    Patch &patch;

    struct SlotData {
        // OUTPUT
        int feedback;
        int output;     // Output value of slot

        // for Phase Generator (PG)
        uint16_t pg_freq;
        unsigned pg_phase;           // Phase counter

        // for Envelope Generator (EG)
        unsigned eg_volume;          // Current volume
        unsigned eg_tll;             // Total Level + Key scale level
        uint16_t eg_rks;
        EnvelopeState eg_state;      // Current state
        EnvPhaseIndex eg_phase;      // Phase
        EnvPhaseIndex eg_dPhase;     // Phase increment amount
        EnvPhaseIndex eg_phase_max;

        bool slot_on_voice;
        bool slot_on_drum;
        bool sustain;                // Sustain

        int sibling; // pointer to sibling slot (only valid for car -> mod)

        int patch;
    };
    SlotData slotData[18];
    SlotData *sd = slotData;
};

} // namespace YM2413Tim
} // namespace openmsx
