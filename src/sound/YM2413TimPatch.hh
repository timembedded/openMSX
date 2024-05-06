/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#pragma once

#include "YM2413TimCommon.hh"

namespace openmsx {
namespace YM2413Tim {

class Patch {
protected:
    Patch();
    static Patch _instance;

public:
    void operator=(const Patch &) = delete;
    static Patch& instance() { return _instance; }

    void select(int voice);
    void reset();

    void initModulator(std::span<const uint8_t, 8> data);
    void initCarrier(std::span<const uint8_t, 8> data);

    /** Sets the Key Scale of Rate (0 or 1). */
    void setKR(uint8_t value);
    /** Sets the frequency multiplier factor [0..15]. */
    void setML(uint8_t value);
    /** Sets Key scale level [0..3]. */
    void setKL(uint8_t value);
    /** Set volume (total level) [0..63]. */
    void setTL(uint8_t value);
    /** Set waveform [0..1]. */
    void setWF(uint8_t value);
    /** Sets the amount of feedback [0..7]. */
    void setFB(uint8_t value);
    /** Sets sustain level [0..15]. */
    void setSL(uint8_t value);

    struct PatchData {
        const uint16_t *WF; // 0-1    transformed to waveform[0-1]
        const uint8_t *KL;  // 0-3    transformed to tllTab[0-3]
        unsigned SL;        // 0-15   transformed to slTable[0-15]
        uint8_t AMPM = 0;   // 0-3    2 packed booleans
        bool EG = false;    // 0-1
        uint8_t KR;         // 0-1    transformed to 10,8
        uint8_t ML;         // 0-15   transformed to mlTable[0-15]
        uint8_t TL;         // 0-63   transformed to TL2EG(0..63) == [0..252]
        uint8_t FB;         // 0,1-7  transformed to 0,7-1
        uint8_t AR = 0;     // 0-15
        uint8_t DR = 0;     // 0-15
        uint8_t RR = 0;     // 0-15
    };
    PatchData patchData[(16+3)*2];
    PatchData *pd = patchData;
};

} // namespace YM2413Tim
} // namespace openmsx
