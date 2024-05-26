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

    struct SignedLiType {
        uint16_t value; // 9 bits
        bool sign;
    };

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

        // VM2413 Envelope Generator
        enum EGState {
            Attack,
            Decay,
            Release,
            Finish
        };
        struct VM2413EnvelopeSlot {
            bool eg_lastkey;
            EGState eg_state;
            uint32_t eg_phase; // 23 bits
            uint32_t eg_dphase; // 23 bits
        };
        VM2413EnvelopeSlot vm2413env;

        // VM2413 Phase Generator
        struct VM2413PhaseSlot {
            bool pg_lastkey;
            uint32_t pg_phase; // 18 bits
        };
        VM2413PhaseSlot vm2413phase;

        // VM2413 OutputGenerator (per channel)
        SignedLiType fdata;
        SignedLiType li_data;
    };
    SlotData slotData[18];
    SlotData *sd = slotData;
    int slot;

    // VM2413 Envelope Generator
    struct vm2413EnvelopeCommon {
        uint32_t ntable; // 18 bits
        uint32_t amphase; // 20 bits
    };
    vm2413EnvelopeCommon vm2413env;
    int16_t attack_multiply(uint8_t i0, int8_t i1);
    uint16_t attack_table(uint32_t addr);

    void vm2413EnvelopeGenerator(
        uint8_t tl,
        uint8_t rr,
        bool key,
        bool rhythm,
        uint16_t &egout // 13 bits
    );

    // VM2413 Phase Generator
    struct vm2413PhaseCommon {
        uint16_t pmcount; // 13 bits
        bool noise14;
        bool noise17;
    };
    vm2413PhaseCommon vm2413phase;

    void vm2413PhaseGenerator(
        bool key, // 1 bit
        bool rhythm, // 1 bit
        bool &noise,
        uint32_t &pgout // 18 bits
    );

    void vm2413SineTable(
        bool wf,
        uint32_t addr, // 18 bits, integer part 9bit, decimal part 9bit
        uint16_t &data // 14 bits, integer part 8bit, decimal part 6bit
    );

    void vm2413Operator(
        bool rhythm,
        bool noise,
        uint32_t pgout, // 18 bits
        uint16_t egout, // 13 bits
        uint16_t &opout  // 14 bits
    );

    void vm2413LinearTable(
        uint32_t addr,        // 14 bits, integer part 8bit, decimal part 6bit
        SignedLiType &data
        );

    void vm2413OutputAverage(
        SignedLiType L,
        SignedLiType R,
        SignedLiType &OUT
        );

    void vm2413OutputGenerator(
        uint16_t opout // 14 bits
    );

    int vm2413GetOutput(int slotnum);
    void vm2413TemporalMixer(
        bool rhythm,
        uint16_t &mo, // 10 bits
        uint16_t &ro  // 10 bits
    );

};

} // namespace YM2413Tim
} // namespace openmsx
