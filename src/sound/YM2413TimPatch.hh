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
public:
    void reset();
    void initModulator(std::span<const uint8_t, 8> data);
    void initCarrier(std::span<const uint8_t, 8> data);

    // PatchData
    bool am = false;   // 0-1
    bool pm = false;   // 0-1
    bool eg = false;   // 0-1
    bool kr = false;   // 0-1   key scale of rate
    uint8_t ml = 0;    // 0-15  frequency multiplier factor
    uint8_t kl = 0;    // 0-3   key scale level
    uint8_t tl = 0;    // 0-63  volume (total level)
    bool wf = false;   // 0-1   waveform
    uint8_t fb = 0;    // 0,1-7 amount of feedback
    uint8_t ar = 0;    // 0-15  attack rate
    uint8_t dr = 0;    // 0-15  decay rate
    uint8_t sl = 0;    // 0-15  sustain level
    uint8_t rr = 0;    // 0-15
};

} // namespace YM2413Tim
} // namespace openmsx
