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
    bool isActive() const;

    void slotOnVoice(bool settle);
    void slotOnRhythm(bool attack, bool settle, bool reset_phase);
    void slotOffRhythm();
    void slotOffVoice();

    template<typename Archive>
    void serialize(Archive& ar, unsigned version);

    struct SignedLiType {
        uint16_t value; // 9 bits
        bool sign;
    };

    struct SlotData {
        // for Phase Generator (PG)
        uint16_t pg_freq;

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

    void vm2413Controller(
        // In
        bool rhythm,
        uint8_t reg_flags,
        uint8_t reg_key,
        uint16_t reg_freq,
        uint16_t reg_patch,
        uint16_t reg_volume,
        uint8_t reg_sustain,
        uint8_t kl,     // 0-3   key scale level
        bool    eg,     // 0-1
        uint8_t tl,     // 0-63  volume (total level)
        uint8_t rr,     // 0-15
        bool    kr,     // 0-1   key scale of rate
        // Out
        bool &kflag,    // 1 bit, key
        uint16_t &fnum, // 9 bits, F-Number
        uint8_t &blk,   // 3 bits, Block
        uint8_t &kll,
        uint8_t &tll,
        uint8_t &rks,   // 4 bits - Rate-KeyScale
        uint8_t &rrr    // 4 bits - Release Rate
    );

    void vm2413EnvelopeGenerator(
        uint8_t tll,
        uint8_t rks,
        uint8_t rrr,
        uint8_t ar,
        uint8_t dr,
        uint8_t sl,
        bool am,
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
        bool pm,
        uint8_t ml, // 4 bits, Multiple
        uint8_t blk, // 3 bits, Block
        uint16_t fnum, // 9 bits, F-Number
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
        bool wf,
        uint8_t fb,  // 3 bits , Feedback
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
