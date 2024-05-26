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

Patch Patch::_instance;

//
// Patch
//
Patch::Patch()
{
}

void Patch::select(int voice)
{
    assert(voice < (int)(sizeof(patchData)/sizeof(patchData[0])));
    pd = &patchData[voice];
}

void Patch::reset()
{
    setWF(0);
    setKL(0);
    setKR(0);
    setML(0);
    setTL(0);
    setFB(0);
    setSL(0);
}

void Patch::initModulator(std::span<const uint8_t, 8> data)
{
    pd->AMPM = (data[0] >> 6) & 3;
    pd->EG = (data[0] >> 5) & 1;
    setKR((data[0] >> 4) & 1);
    setML((data[0] >> 0) & 15);
    setKL((data[2] >> 6) & 3);
    setTL((data[2] >> 0) & 63);
    setWF((data[3] >> 3) & 1);
    setFB((data[3] >> 0) & 7);
    pd->AR = (data[4] >> 4) & 15;
    pd->DR = (data[4] >> 0) & 15;
    setSL((data[6] >> 4) & 15);
    pd->RR = (data[6] >> 0) & 15;
}

void Patch::initCarrier(std::span<const uint8_t, 8> data)
{
    pd->AMPM = (data[1] >> 6) & 3;
    pd->EG = (data[1] >> 5) & 1;
    setKR((data[1] >> 4) & 1);
    setML((data[1] >> 0) & 15);
    setKL((data[3] >> 6) & 3);
    setTL(0);
    setWF((data[3] >> 4) & 1);
    setFB(0);
    pd->AR = (data[5] >> 4) & 15;
    pd->DR = (data[5] >> 0) & 15;
    setSL((data[7] >> 4) & 15);
    pd->RR = (data[7] >> 0) & 15;
}

void Patch::setKR(uint8_t value)
{
    pd->_kr = value;
    pd->KR = value ? 8 : 10;
}
void Patch::setML(uint8_t value)
{
    pd->_ml = value & 15;
    pd->ML = mlTable[value];
}
void Patch::setKL(uint8_t value)
{
    pd->_kl = value & 3;
    pd->KL = tllTab[value];
}
void Patch::setTL(uint8_t value)
{
    assert(value < 64);
    pd->_tl = value & 0x3f;
    pd->TL = narrow<uint8_t>(TL2EG(value));
}
void Patch::setWF(uint8_t value)
{
    pd->_wf = value & 1;
    pd->WF = waveform[value];
}
void Patch::setFB(uint8_t value)
{
    pd->_fb = value & 7;
    pd->FB = value ? 8 - value : 0;
}
void Patch::setSL(uint8_t value)
{
    pd->_sl = value & 15;
    pd->SL = slTab[value];
}

} // namespace YM2413Tim
} // namespace openmsx
