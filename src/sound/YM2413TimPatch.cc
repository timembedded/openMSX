/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#pragma once

#include "YM2413TimPatch.hh"
#include "YM2413TimCommon.hh"

namespace openmsx {
namespace YM2413Tim {

//
// Patch
//

void Patch::reset()
{
    wf = 0;
    kl = 0;
    kr = 0;
    ml = 0;
    tl = 0;
    fb = 0;
    sl = 0;
}

void Patch::initModulator(std::span<const uint8_t, 8> data)
{
    am = (data[0] >> 7) & 1;
    pm = (data[0] >> 6) & 1;
    eg = (data[0] >> 5) & 1;
    kr = (data[0] >> 4) & 1;
    ml = (data[0] >> 0) & 15;
    kl = (data[2] >> 6) & 3;
    tl = (data[2] >> 0) & 63;
    wf = (data[3] >> 3) & 1;
    fb = (data[3] >> 0) & 7;
    ar = (data[4] >> 4) & 15;
    dr = (data[4] >> 0) & 15;
    sl = (data[6] >> 4) & 15;
    rr = (data[6] >> 0) & 15;
}

void Patch::initCarrier(std::span<const uint8_t, 8> data)
{
    am = (data[1] >> 7) & 1;
    pm = (data[1] >> 6) & 1;
    eg = (data[1] >> 5) & 1;
    kr = (data[1] >> 4) & 1;
    ml = (data[1] >> 0) & 15;
    kl = (data[3] >> 6) & 3;
    tl = 0;
    wf = (data[3] >> 4) & 1;
    fb = 0;
    ar = (data[5] >> 4) & 15;
    dr = (data[5] >> 0) & 15;
    sl = (data[7] >> 4) & 15;
    rr = (data[7] >> 0) & 15;
}

} // namespace YM2413Tim
} // namespace openmsx
