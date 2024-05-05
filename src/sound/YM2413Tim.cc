/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#include "YM2413Tim.hh"
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
// YM2413
//

static constexpr std::array inst_data = {
    std::array<uint8_t, 8>{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, // user instrument
    std::array<uint8_t, 8>{ 0x61,0x61,0x1e,0x17,0xf0,0x7f,0x00,0x17 }, // violin
    std::array<uint8_t, 8>{ 0x13,0x41,0x16,0x0e,0xfd,0xf4,0x23,0x23 }, // guitar
    std::array<uint8_t, 8>{ 0x03,0x01,0x9a,0x04,0xf3,0xf3,0x13,0xf3 }, // piano
    std::array<uint8_t, 8>{ 0x11,0x61,0x0e,0x07,0xfa,0x64,0x70,0x17 }, // flute
    std::array<uint8_t, 8>{ 0x22,0x21,0x1e,0x06,0xf0,0x76,0x00,0x28 }, // clarinet
    std::array<uint8_t, 8>{ 0x21,0x22,0x16,0x05,0xf0,0x71,0x00,0x18 }, // oboe
    std::array<uint8_t, 8>{ 0x21,0x61,0x1d,0x07,0x82,0x80,0x17,0x17 }, // trumpet
    std::array<uint8_t, 8>{ 0x23,0x21,0x2d,0x16,0x90,0x90,0x00,0x07 }, // organ
    std::array<uint8_t, 8>{ 0x21,0x21,0x1b,0x06,0x64,0x65,0x10,0x17 }, // horn
    std::array<uint8_t, 8>{ 0x21,0x21,0x0b,0x1a,0x85,0xa0,0x70,0x07 }, // synthesizer
    std::array<uint8_t, 8>{ 0x23,0x01,0x83,0x10,0xff,0xb4,0x10,0xf4 }, // harpsichord
    std::array<uint8_t, 8>{ 0x97,0xc1,0x20,0x07,0xff,0xf4,0x22,0x22 }, // vibraphone
    std::array<uint8_t, 8>{ 0x61,0x00,0x0c,0x05,0xc2,0xf6,0x40,0x44 }, // synthesizer bass
    std::array<uint8_t, 8>{ 0x01,0x01,0x56,0x03,0x94,0xc2,0x03,0x12 }, // acoustic bass
    std::array<uint8_t, 8>{ 0x21,0x01,0x89,0x03,0xf1,0xe4,0xf0,0x23 }, // electric guitar
    std::array<uint8_t, 8>{ 0x07,0x21,0x14,0x00,0xee,0xf8,0xff,0xf8 },
    std::array<uint8_t, 8>{ 0x01,0x31,0x00,0x00,0xf8,0xf7,0xf8,0xf7 },
    std::array<uint8_t, 8>{ 0x25,0x11,0x00,0x00,0xf8,0xfa,0xf8,0x55 }
};

YM2413::YM2413()
{
    ranges::fill(reg, 0); // avoid UMR

    for (auto i : xrange(16 + 3)) {
        patches[i][0].initModulator(inst_data[i]);
        patches[i][1].initCarrier(inst_data[i]);
    }

    reset();
}

// Reset whole of OPLL except patch data
void YM2413::reset()
{
    pm_phase = 0;
    am_phase = 0;
    noise_seed = 0xFFFF;

    for (auto& ch : channels) {
        ch.reset();
        ch.setPatch(getPatch(0, false), getPatch(0, true));
    }
    for (auto i : xrange(uint8_t(0x40))) {
        writeReg(i, 0);
    }
    registerLatch = 0;
}

// Drum key on
void YM2413::keyOn_BD()
{
    Channel& ch6 = channels[6];
    if (!ch6.car.slot_on_flag) {
        ch6.car.setEnvelopeState(SETTLE);
        // this will shortly set both car and mod to ATTACK state
    }
    ch6.car.slot_on_flag |= 2;
    ch6.mod.slot_on_flag |= 2;
}
void YM2413::keyOn_HH()
{
    // TODO do these also use the SETTLE stuff?
    Channel& ch7 = channels[7];
    if (!ch7.mod.slot_on_flag) {
        ch7.mod.slotOn2();
    }
    ch7.mod.slot_on_flag |= 2;
}
void YM2413::keyOn_SD()
{
    Channel& ch7 = channels[7];
    if (!ch7.car.slot_on_flag) {
        ch7.car.slotOn();
    }
    ch7.car.slot_on_flag |= 2;
}
void YM2413::keyOn_TOM()
{
    Channel& ch8 = channels[8];
    if (!ch8.mod.slot_on_flag) {
        ch8.mod.slotOn();
    }
    ch8.mod.slot_on_flag |= 2;
}
void YM2413::keyOn_CYM()
{
    Channel& ch8 = channels[8];
    if (!ch8.car.slot_on_flag) {
        ch8.car.slotOn2();
    }
    ch8.car.slot_on_flag |= 2;
}

// Drum key off
void YM2413::keyOff_BD()
{
    Channel& ch6 = channels[6];
    if (ch6.car.slot_on_flag) {
        ch6.car.slot_on_flag &= ~2;
        ch6.mod.slot_on_flag &= ~2;
        if (!ch6.car.slot_on_flag) {
            ch6.car.slotOff();
        }
    }
}
void YM2413::keyOff_HH()
{
    Channel& ch7 = channels[7];
    if (ch7.mod.slot_on_flag) {
        ch7.mod.slot_on_flag &= ~2;
        if (!ch7.mod.slot_on_flag) {
            ch7.mod.slotOff();
        }
    }
}
void YM2413::keyOff_SD()
{
    Channel& ch7 = channels[7];
    if (ch7.car.slot_on_flag) {
        ch7.car.slot_on_flag &= ~2;
        if (!ch7.car.slot_on_flag) {
            ch7.car.slotOff();
        }
    }
}
void YM2413::keyOff_TOM()
{
    Channel& ch8 = channels[8];
    if (ch8.mod.slot_on_flag) {
        ch8.mod.slot_on_flag &= ~2;
        if (!ch8.mod.slot_on_flag) {
            ch8.mod.slotOff();
        }
    }
}
void YM2413::keyOff_CYM()
{
    Channel& ch8 = channels[8];
    if (ch8.car.slot_on_flag) {
        ch8.car.slot_on_flag &= ~2;
        if (!ch8.car.slot_on_flag) {
            ch8.car.slotOff();
        }
    }
}

void YM2413::setRhythmFlags(uint8_t old)
{
    Channel& ch6 = channels[6];
    Channel& ch7 = channels[7];
    Channel& ch8 = channels[8];

    // flags = X | X | mode | BD | SD | TOM | TC | HH
    uint8_t flags = reg[0x0E];
    if ((flags ^ old) & 0x20) {
        if (flags & 0x20) {
            // OFF -> ON
            ch6.setPatch(getPatch(16, false), getPatch(16, true));
            ch7.setPatch(getPatch(17, false), getPatch(17, true));
            ch7.mod.setVolume(reg[0x37] >> 4);
            ch8.setPatch(getPatch(18, false), getPatch(18, true));
            ch8.mod.setVolume(reg[0x38] >> 4);
        }
        else {
            // ON -> OFF
            ch6.setPatch(getPatch(reg[0x36] >> 4, false), getPatch(reg[0x36] >> 4, true));
            keyOff_BD();
            ch7.setPatch(getPatch(reg[0x37] >> 4, false), getPatch(reg[0x37] >> 4, true));
            keyOff_SD();
            keyOff_HH();
            ch8.setPatch(getPatch(reg[0x38] >> 4, false), getPatch(reg[0x38] >> 4, true));
            keyOff_TOM();
            keyOff_CYM();
        }
    }
    if (flags & 0x20) {
        if (flags & 0x10) keyOn_BD();  else keyOff_BD();
        if (flags & 0x08) keyOn_SD();  else keyOff_SD();
        if (flags & 0x04) keyOn_TOM(); else keyOff_TOM();
        if (flags & 0x02) keyOn_CYM(); else keyOff_CYM();
        if (flags & 0x01) keyOn_HH();  else keyOff_HH();
    }

    uint16_t freq6 = getFreq(6);
    ch6.mod.updateAll(freq6, false);
    ch6.car.updateAll(freq6, true);
    uint16_t freq7 = getFreq(7);
    ch7.mod.updateAll(freq7, isRhythm());
    ch7.car.updateAll(freq7, true);
    uint16_t freq8 = getFreq(8);
    ch8.mod.updateAll(freq8, isRhythm());
    ch8.car.updateAll(freq8, true);
}

void YM2413::update_key_status()
{
    for (auto [i, ch] : enumerate(channels)) {
        uint8_t slot_on = (reg[0x20 + i] & 0x10) ? 1 : 0;
        ch.mod.slot_on_flag = slot_on;
        ch.car.slot_on_flag = slot_on;
    }
    if (isRhythm()) {
        Channel& ch6 = channels[6];
        ch6.mod.slot_on_flag |= uint8_t((reg[0x0e] & 0x10) ? 2 : 0); // BD1
        ch6.car.slot_on_flag |= uint8_t((reg[0x0e] & 0x10) ? 2 : 0); // BD2
        Channel& ch7 = channels[7];
        ch7.mod.slot_on_flag |= uint8_t((reg[0x0e] & 0x01) ? 2 : 0); // HH
        ch7.car.slot_on_flag |= uint8_t((reg[0x0e] & 0x08) ? 2 : 0); // SD
        Channel& ch8 = channels[8];
        ch8.mod.slot_on_flag |= uint8_t((reg[0x0e] & 0x04) ? 2 : 0); // TOM
        ch8.car.slot_on_flag |= uint8_t((reg[0x0e] & 0x02) ? 2 : 0); // CYM
    }
}

float YM2413::getAmplificationFactor() const
{
    return 1.0f / (1 << DB2LIN_AMP_BITS);
}

bool YM2413::isRhythm() const
{
    return (reg[0x0E] & 0x20) != 0;
}

uint16_t YM2413::getFreq(unsigned channel) const
{
    // combined fnum (=9bit) and block (=3bit)
    assert(channel < 9);
    return reg[0x10 + channel] | ((reg[0x20 + channel] & 0x0F) << 8);
}

Patch& YM2413::getPatch(unsigned instrument, bool carrier)
{
    return patches[instrument][carrier];
}

void YM2413::calcChannel(Channel& ch, uint8_t FLAGS, std::span<float> buf)
{
    // VC++ requires explicit conversion to bool. Compiler bug??
    const bool HAS_CAR_PM = (FLAGS & 1) != 0;
    const bool HAS_CAR_AM = (FLAGS & 2) != 0;
    const bool HAS_MOD_PM = (FLAGS & 4) != 0;
    const bool HAS_MOD_AM = (FLAGS & 8) != 0;
    const bool HAS_MOD_FB = (FLAGS & 16) != 0;
    const bool HAS_CAR_FIXED_ENV = (FLAGS & 32) != 0;
    const bool HAS_MOD_FIXED_ENV = (FLAGS & 64) != 0;

    assert(((ch.car.patch.AMPM & 1) != 0) == HAS_CAR_PM);
    assert(((ch.car.patch.AMPM & 2) != 0) == HAS_CAR_AM);
    assert(((ch.mod.patch.AMPM & 1) != 0) == HAS_MOD_PM);
    assert(((ch.mod.patch.AMPM & 2) != 0) == HAS_MOD_AM);

    unsigned tmp_pm_phase = pm_phase;
    unsigned tmp_am_phase = am_phase;
    unsigned car_fixed_env = 0; // dummy
    unsigned mod_fixed_env = 0; // dummy
    if (HAS_CAR_FIXED_ENV) {
        car_fixed_env = ch.car.calc_fixed_env(HAS_CAR_AM);
    }
    if (HAS_MOD_FIXED_ENV) {
        mod_fixed_env = ch.mod.calc_fixed_env(HAS_MOD_AM);
    }

    for (auto& b : buf) {
        unsigned lfo_pm = 0;
        if (HAS_CAR_PM || HAS_MOD_PM) {
            // Copied from Burczynski:
            //  There are only 8 different steps for PM, and each
            //  step lasts for 1024 samples. This results in a PM
            //  freq of 6.1Hz (but datasheet says it's 6.4Hz).
            ++tmp_pm_phase;
            lfo_pm = (tmp_pm_phase >> 10) & 7;
        }
        int lfo_am = 0; // avoid warning
        if (HAS_CAR_AM || HAS_MOD_AM) {
            ++tmp_am_phase;
            if (tmp_am_phase == (LFO_AM_TAB_ELEMENTS * 64)) {
                tmp_am_phase = 0;
            }
            lfo_am = lfo_am_table[tmp_am_phase / 64];
        }
        int fm = ch.mod.calc_slot_mod(HAS_MOD_AM, HAS_MOD_FB, HAS_MOD_FIXED_ENV,
            HAS_MOD_PM ? lfo_pm : 0, lfo_am, mod_fixed_env);
        b += narrow_cast<float>(ch.car.calc_slot_car(HAS_CAR_AM, HAS_CAR_FIXED_ENV,
            HAS_CAR_PM ? lfo_pm : 0, lfo_am, fm, car_fixed_env));
    }
}

void YM2413::generateChannels(std::span<float*, 9 + 5> bufs, unsigned num)
{
    assert(num != 0);

    for (auto i : xrange(isRhythm() ? 6 : 9)) {
        Channel& ch = channels[i];
        if (ch.car.isActive()) {
            bool carFixedEnv = ch.car.state == one_of(SUSHOLD, FINISH);
            bool modFixedEnv = ch.mod.state == one_of(SUSHOLD, FINISH);
            if (ch.car.state == SETTLE) {
                modFixedEnv = false;
            }
            unsigned flags = (ch.car.patch.AMPM << 0) |
                (ch.mod.patch.AMPM << 2) |
                ((ch.mod.patch.FB != 0) << 4) |
                (carFixedEnv << 5) |
                (modFixedEnv << 6);
            calcChannel(ch, flags, { bufs[i], num });
        }
        else {
            bufs[i] = nullptr;
        }
    }
    // update AM, PM unit
    pm_phase += num;
    am_phase = (am_phase + num) % (LFO_AM_TAB_ELEMENTS * 64);

    if (isRhythm()) {
        bufs[6] = nullptr;
        bufs[7] = nullptr;
        bufs[8] = nullptr;

        Channel& ch6 = channels[6];
        Channel& ch7 = channels[7];
        Channel& ch8 = channels[8];

        unsigned old_noise = noise_seed;
        unsigned old_cPhase7 = ch7.mod.cPhase;
        unsigned old_cPhase8 = ch8.car.cPhase;

        if (ch6.car.isActive()) {
            for (auto sample : xrange(num)) {
                bufs[9][sample] += narrow_cast<float>(
                    2 * ch6.car.calc_slot_car(false, false,
                        0, 0, ch6.mod.calc_slot_mod(
                        false, false, false, 0, 0, 0), 0));
            }
        }
        else {
            bufs[9] = nullptr;
        }

        if (ch7.car.isActive()) {
            for (auto sample : xrange(num)) {
                noise_seed >>= 1;
                bool noise_bit = noise_seed & 1;
                if (noise_bit) noise_seed ^= 0x8003020;
                bufs[10][sample] += narrow_cast<float>(
                    -2 * ch7.car.calc_slot_snare(noise_bit));
            }
        }
        else {
            bufs[10] = nullptr;
        }

        if (ch8.car.isActive()) {
            for (auto sample : xrange(num)) {
                unsigned phase7 = ch7.mod.calc_phase(0);
                unsigned phase8 = ch8.car.calc_phase(0);
                bufs[11][sample] += narrow_cast<float>(
                    -2 * ch8.car.calc_slot_cym(phase7, phase8));
            }
        }
        else {
            bufs[11] = nullptr;
        }

        if (ch7.mod.isActive()) {
            // restore noise, ch7/8 cPhase
            noise_seed = old_noise;
            ch7.mod.cPhase = old_cPhase7;
            ch8.car.cPhase = old_cPhase8;
            for (auto sample : xrange(num)) {
                noise_seed >>= 1;
                bool noise_bit = noise_seed & 1;
                if (noise_bit) noise_seed ^= 0x8003020;
                unsigned phase7 = ch7.mod.calc_phase(0);
                unsigned phase8 = ch8.car.calc_phase(0);
                bufs[12][sample] += narrow_cast<float>(
                    2 * ch7.mod.calc_slot_hat(phase7, phase8, noise_bit));
            }
        }
        else {
            bufs[12] = nullptr;
        }

        if (ch8.mod.isActive()) {
            for (auto sample : xrange(num)) {
                bufs[13][sample] += narrow_cast<float>(
                    2 * ch8.mod.calc_slot_tom());
            }
        }
        else {
            bufs[13] = nullptr;
        }
    }
    else {
        bufs[9] = nullptr;
        bufs[10] = nullptr;
        bufs[11] = nullptr;
        bufs[12] = nullptr;
        bufs[13] = nullptr;
    }
}

void YM2413::writePort(bool port, uint8_t value, int /*offset*/)
{
    if (port == 0) {
        registerLatch = value;
    }
    else {
        writeReg(registerLatch & 0x3f, value);
    }
}

void YM2413::pokeReg(uint8_t r, uint8_t data)
{
    writeReg(r, data);
}

void YM2413::writeReg(uint8_t r, uint8_t data)
{
    assert(r < 0x40);

    switch (r) {
    case 0x00: {
        reg[r] = data;
        patches[0][0].AMPM = (data >> 6) & 3;
        patches[0][0].EG = (data >> 5) & 1;
        patches[0][0].setKR((data >> 4) & 1);
        patches[0][0].setML((data >> 0) & 15);
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                uint16_t freq = getFreq(i);
                ch.mod.updatePG(freq);
                ch.mod.updateRKS(freq);
                ch.mod.updateEG();
            }
        }
        break;
    }
    case 0x01: {
        reg[r] = data;
        patches[0][1].AMPM = (data >> 6) & 3;
        patches[0][1].EG = (data >> 5) & 1;
        patches[0][1].setKR((data >> 4) & 1);
        patches[0][1].setML((data >> 0) & 15);
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                uint16_t freq = getFreq(i);
                ch.car.updatePG(freq);
                ch.car.updateRKS(freq);
                ch.car.updateEG();
            }
        }
        break;
    }
    case 0x02: {
        reg[r] = data;
        patches[0][0].setKL((data >> 6) & 3);
        patches[0][0].setTL((data >> 0) & 63);
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                bool actAsCarrier = (i >= 7) && isRhythm();
                assert(!actAsCarrier); (void)actAsCarrier;
                ch.mod.updateTLL(getFreq(i), false);
            }
        }
        break;
    }
    case 0x03: {
        reg[r] = data;
        patches[0][1].setKL((data >> 6) & 3);
        patches[0][1].setWF((data >> 4) & 1);
        patches[0][0].setWF((data >> 3) & 1);
        patches[0][0].setFB((data >> 0) & 7);
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
            }
        }
        break;
    }
    case 0x04: {
        reg[r] = data;
        patches[0][0].AR = (data >> 4) & 15;
        patches[0][0].DR = (data >> 0) & 15;
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                ch.mod.updateEG();
                if (ch.mod.state == ATTACK) {
                    ch.mod.setEnvelopeState(ATTACK);
                }
            }
        }
        break;
    }
    case 0x05: {
        reg[r] = data;
        patches[0][1].AR = (data >> 4) & 15;
        patches[0][1].DR = (data >> 0) & 15;
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                ch.car.updateEG();
                if (ch.car.state == ATTACK) {
                    ch.car.setEnvelopeState(ATTACK);
                }
            }
        }
        break;
    }
    case 0x06: {
        reg[r] = data;
        patches[0][0].setSL((data >> 4) & 15);
        patches[0][0].RR = (data >> 0) & 15;
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                ch.mod.updateEG();
                if (ch.mod.state == DECAY) {
                    ch.mod.setEnvelopeState(DECAY);
                }
            }
        }
        break;
    }
    case 0x07: {
        reg[r] = data;
        patches[0][1].setSL((data >> 4) & 15);
        patches[0][1].RR = (data >> 0) & 15;
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if ((reg[0x30 + i] & 0xF0) == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                ch.car.updateEG();
                if (ch.car.state == DECAY) {
                    ch.car.setEnvelopeState(DECAY);
                }
            }
        }
        break;
    }
    case 0x0E: {
        uint8_t old = reg[r];
        reg[r] = data;
        setRhythmFlags(old);
        break;
    }
    case 0x19: case 0x1A: case 0x1B: case 0x1C:
    case 0x1D: case 0x1E: case 0x1F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: {
        reg[r] = data;
        unsigned cha = r & 0x0F; assert(cha < 9);
        Channel& ch = channels[cha];
        bool actAsCarrier = (cha >= 7) && isRhythm();
        uint16_t freq = getFreq(cha);
        ch.mod.updateAll(freq, actAsCarrier);
        ch.car.updateAll(freq, true);
        break;
    }
    case 0x29: case 0x2A: case 0x2B: case 0x2C:
    case 0x2D: case 0x2E: case 0x2F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
    case 0x25: case 0x26: case 0x27: case 0x28: {
        reg[r] = data;
        unsigned cha = r & 0x0F; assert(cha < 9);
        Channel& ch = channels[cha];
        bool modActAsCarrier = (cha >= 7) && isRhythm();
        ch.setSustain((data >> 5) & 1, modActAsCarrier);
        if (data & 0x10) {
            ch.keyOn();
        }
        else {
            ch.keyOff();
        }
        uint16_t freq = getFreq(cha);
        ch.mod.updateAll(freq, modActAsCarrier);
        ch.car.updateAll(freq, true);
        break;
    }
    case 0x39: case 0x3A: case 0x3B: case 0x3C:
    case 0x3D: case 0x3E: case 0x3F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
    case 0x35: case 0x36: case 0x37: case 0x38: {
        reg[r] = data;
        unsigned cha = r & 0x0F; assert(cha < 9);
        Channel& ch = channels[cha];
        if (isRhythm() && (cha >= 6)) {
            if (cha > 6) {
                // channel 7 or 8 in rythm mode
                ch.mod.setVolume(data >> 4);
            }
        }
        else {
            ch.setPatch(getPatch(data >> 4, false), getPatch(data >> 4, true));
        }
        ch.car.setVolume(data & 15);
        bool actAsCarrier = (cha >= 7) && isRhythm();
        uint16_t freq = getFreq(cha);
        ch.mod.updateAll(freq, actAsCarrier);
        ch.car.updateAll(freq, true);
        break;
    }
    default:
        break;
    }
}

uint8_t YM2413::peekReg(uint8_t /*r*/) const
{
    return 0xff; //reg[r]; The original YM2413 does not allow reading back registers
}

} // namespace YM2413Tim

static constexpr std::initializer_list<enum_string<YM2413Tim::EnvelopeState>> envelopeStateInfo = {
    { "ATTACK",  YM2413Tim::ATTACK  },
    { "DECAY",   YM2413Tim::DECAY   },
    { "SUSHOLD", YM2413Tim::SUSHOLD },
    { "SUSTAIN", YM2413Tim::SUSTAIN },
    { "RELEASE", YM2413Tim::RELEASE },
    { "SETTLE",  YM2413Tim::SETTLE  },
    { "FINISH",  YM2413Tim::FINISH  }
};
SERIALIZE_ENUM(YM2413Tim::EnvelopeState, envelopeStateInfo);

namespace YM2413Tim {

    // version 1: initial version
    // version 2: don't serialize "type / actAsCarrier" anymore, it's now
    //            a calculated value
    // version 3: don't serialize slot_on_flag anymore
    // version 4: don't serialize volume anymore
    template<typename Archive>
    void Slot::serialize(Archive& ar, unsigned /*version*/)
    {
        ar.serialize("feedback", feedback,
            "output", output,
            "cphase", cPhase,
            "state", state,
            "eg_phase", eg_phase,
            "sustain", sustain);

        // These are restored by calls to
        //  updateAll():         eg_dPhase, dPhaseDRTableRks, tll, dPhase
        //  setEnvelopeState():  eg_phase_max
        //  setPatch():          patch
        //  setVolume():         volume
        //  update_key_status(): slot_on_flag
    }

    // version 1: initial version
    // version 2: removed patch_number, freq
    template<typename Archive>
    void Channel::serialize(Archive& ar, unsigned /*version*/)
    {
        ar.serialize("mod", mod,
            "car", car);
    }


    // version 1: initial version
    // version 2: 'registers' are moved here (no longer serialized in base class)
    // version 3: no longer serialize 'user_patch_mod' and 'user_patch_car'
    // version 4: added 'registerLatch'
    template<typename Archive>
    void YM2413::serialize(Archive& ar, unsigned version)
    {
        if (ar.versionBelow(version, 2)) ar.beginTag("YM2413Core");
        ar.serialize("registers", reg);
        if (ar.versionBelow(version, 2)) ar.endTag("YM2413Core");

        // no need to serialize patches[]
        //   patches[0] is restored from registers, the others are read-only
        ar.serialize("channels", channels,
            "pm_phase", pm_phase,
            "am_phase", am_phase,
            "noise_seed", noise_seed);

        if constexpr (Archive::IS_LOADER) {
            patches[0][0].initModulator(subspan<8>(reg));
            patches[0][1].initCarrier(subspan<8>(reg));
            for (auto [i, ch] : enumerate(channels)) {
                // restore patch
                unsigned p = ((i >= 6) && isRhythm())
                    ? unsigned(16 + (i - 6))
                    : (reg[0x30 + i] >> 4);
                ch.setPatch(getPatch(p, false), getPatch(p, true)); // before updateAll()
                // restore volume
                ch.car.setVolume(reg[0x30 + i] & 15);
                if (isRhythm() && (i >= 7)) { // ch 7/8 rythm
                    ch.mod.setVolume(reg[0x30 + i] >> 4);
                }
                // sync various variables
                bool actAsCarrier = (i >= 7) && isRhythm();
                uint16_t freq = getFreq(unsigned(i));
                ch.mod.updateAll(freq, actAsCarrier);
                ch.car.updateAll(freq, true);
                ch.mod.setEnvelopeState(ch.mod.state);
                ch.car.setEnvelopeState(ch.car.state);
            }
            update_key_status();
        }
        if (ar.versionAtLeast(version, 4)) {
            ar.serialize("registerLatch", registerLatch);
        }
        else {
            // could be restored from MSXMusicBase, worth the effort?
        }
    }

} // namespace YM2413Tim

using YM2413Tim::YM2413;
INSTANTIATE_SERIALIZE_METHODS(YM2413);
REGISTER_POLYMORPHIC_INITIALIZER(YM2413Core, YM2413, "YM2413-Tim");

} // namespace openmsx
