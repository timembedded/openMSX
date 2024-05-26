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

YM2413::YM2413() :
    slot(Slot::instance()),
    patch(Patch::instance())
{
    ranges::fill(reg_instr, 0); // avoid UMR
    ranges::fill(reg_freq, 0); // avoid UMR
    ranges::fill(reg_patch, 0); // avoid UMR
    ranges::fill(reg_volume, 0); // avoid UMR

    for (int i = 0; i < 9; i++) {
        channels[i].mod = (i << 1);
        channels[i].car = (i << 1) + 1;
    }

    for (auto i : xrange(16 + 3)) {
        patch.select(getPatch(i, false));
        patch.reset();
        patch.initModulator(inst_data[i]);
        patch.select(getPatch(i, true));
        patch.reset();
        patch.initCarrier(inst_data[i]);
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

void YM2413::setRhythmFlags(uint8_t flags)
{
    Channel& ch6 = channels[6];
    Channel& ch7 = channels[7];
    Channel& ch8 = channels[8];
    bool turn_rhythm_on = false;
    bool turn_rhythm_off = false;
    bool key_bd = false;
    bool key_sd = false;
    bool key_tom = false;
    bool key_cym = false;
    bool key_hh = false;

    uint8_t old = reg_flags;
    reg_flags = flags;

    // flags = X | X | mode | BD | SD | TOM | TC | HH
    if ((flags ^ old) & 0x20) {
        if (flags & 0x20) {
            // OFF -> ON
            turn_rhythm_on = true;
        }
        else {
            // ON -> OFF
            turn_rhythm_off = true;
        }
    }
    if (flags & 0x20) {
        key_bd = (flags & 0x10);
        key_sd = (flags & 0x08);
        key_tom = (flags & 0x04);
        key_cym = (flags & 0x02);
        key_hh = (flags & 0x01);
    }

    // CH6 - key BD
    if (turn_rhythm_on) {
        ch6.setPatch(getPatch(16, false), getPatch(16, true));
    }
    if (turn_rhythm_off) {
        ch6.setPatch(getPatch(reg_patch[6], false), getPatch(reg_patch[6], true));
    }
    uint16_t freq6 = getFreq(6);
    slot.select(channels[6].car);
    slot.updateAll(freq6, true);
    if (key_bd) {
        slot.slotOnRhythm(false, true, false); // this will shortly set both car and mod to ATTACK state
    }else{
        slot.slotOffRhythm();
    }
    slot.select(channels[6].mod);
    slot.updateAll(freq6, false);
    if (key_bd) {
        slot.slotOnRhythm(false, false, false);
    }else{
        slot.slotOffRhythm();
    }

    // CH7 - key SD+HH
    if (turn_rhythm_on) {
        ch7.setPatch(getPatch(17, false), getPatch(17, true));
    }
    if (turn_rhythm_off) {
        ch7.setPatch(getPatch(reg_patch[7], false), getPatch(reg_patch[7], true));
    }
    uint16_t freq7 = getFreq(7);
    slot.select(channels[7].car);
    slot.updateAll(freq7, true);
    if (key_sd) {
        slot.slotOnRhythm(true, false, true);
    }else{
        slot.slotOffRhythm();
    }
    slot.select(channels[7].mod);
    slot.updateAll(freq7, isRhythm());
    if (turn_rhythm_on) {
        slot.setVolume(reg_patch[7]);
    }
    if (key_hh) {
        // TODO do these also use the SETTLE stuff?
        slot.slotOnRhythm(true, false, false);
    }else{
        slot.slotOffRhythm();
    }

    // CH8 - key CYM+TOM
    if (turn_rhythm_on) {
        ch8.setPatch(getPatch(18, false), getPatch(18, true));
    }
    if (turn_rhythm_off) {
        ch8.setPatch(getPatch(reg_patch[8], false), getPatch(reg_patch[8], true));
    }
    uint16_t freq8 = getFreq(8);
    slot.select(channels[8].car);
    slot.updateAll(freq8, true);
    if (key_cym) {
        slot.slotOnRhythm(true, false, false);
    }else{
        slot.slotOffRhythm();
    }
    slot.select(channels[8].mod);
    slot.updateAll(freq8, isRhythm());
    if (turn_rhythm_on) {
        slot.setVolume(reg_patch[8]);
    }
    if (key_tom) {
        slot.slotOnRhythm(true, false, true);
    }else{
        slot.slotOffRhythm();
    }
}

void YM2413::update_key_status()
{
    for (auto [i, ch] : enumerate(channels)) {
        if (reg_key[i]) {
            ch.keyOn();
        }else{
            ch.keyOff();
        }
    }
    if (isRhythm()) {
        Channel& ch6 = channels[6];
        slot.select(ch6.mod);
        if (reg_flags & 0x10) { // BD1
            slot.slotOnRhythm(false, false, false);
        }
        slot.select(ch6.car);
        if (reg_flags & 0x10) { // BD2
            slot.slotOnRhythm(false, false, false);
        }
        Channel& ch7 = channels[7];
        slot.select(ch7.mod);
        if (reg_flags & 0x01) { // HH
            slot.slotOnRhythm(false, false, false);
        }
        slot.select(ch7.car);
        if (reg_flags & 0x08) { // SD
            slot.slotOnRhythm(false, false, false);
        }
        Channel& ch8 = channels[8];
        slot.select(ch8.mod);
        if (reg_flags & 0x04) { // TOM
            slot.slotOnRhythm(false, false, false);
        }
        slot.select(ch8.car);
        if (reg_flags & 0x02) { // CYM
            slot.slotOnRhythm(false, false, false);
        }
    }
}

float YM2413::getAmplificationFactor() const
{
    return 1.0f / (1 << DB2LIN_AMP_BITS);
}

bool YM2413::isRhythm() const
{
    return (reg_flags & 0x20) != 0;
}

uint16_t YM2413::getFreq(unsigned channel) const
{
    // combined fnum (=9bit) and block (=3bit)
    assert(channel < 9);
    return reg_freq[channel];
}

int YM2413::getPatch(unsigned instrument, bool carrier)
{
    return (instrument << 1) + (carrier? 1:0);
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

    unsigned tmp_pm_phase = pm_phase;
    unsigned tmp_am_phase = am_phase;
    unsigned car_fixed_env = 0; // dummy
    unsigned mod_fixed_env = 0; // dummy

    slot.select(ch.car);
    assert(((slot.patch.pd->AMPM & 1) != 0) == HAS_CAR_PM);
    assert(((slot.patch.pd->AMPM & 2) != 0) == HAS_CAR_AM);
    if (HAS_CAR_FIXED_ENV) {
        car_fixed_env = slot.calc_fixed_env(HAS_CAR_AM);
    }

    slot.select(ch.mod);
    assert(((slot.patch.pd->AMPM & 1) != 0) == HAS_MOD_PM);
    assert(((slot.patch.pd->AMPM & 2) != 0) == HAS_MOD_AM);
    if (HAS_MOD_FIXED_ENV) {
        mod_fixed_env = slot.calc_fixed_env(HAS_MOD_AM);
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
        slot.select(ch.mod);
        int fm = slot.calc_slot_mod(HAS_MOD_AM, HAS_MOD_FB, HAS_MOD_FIXED_ENV,
            HAS_MOD_PM ? lfo_pm : 0, lfo_am, mod_fixed_env);
        slot.select(ch.car);
        b += narrow_cast<float>(slot.calc_slot_car(HAS_CAR_AM, HAS_CAR_FIXED_ENV,
            HAS_CAR_PM ? lfo_pm : 0, lfo_am, fm, car_fixed_env));
    }
}

static const uint8_t kl_table[16] = {
    0b000000, 0b011000, 0b100000, 0b100101,
    0b101000, 0b101011, 0b101101, 0b101111,
    0b110000, 0b110010, 0b110011, 0b110100,
    0b110101, 0b110110, 0b110111, 0b111000
}; // 0.75db/step, 6db/oct

void YM2413::generateChannelsVM2413(std::span<float*, 9 + 5> bufs, unsigned num)
{
    for (auto sample : xrange(num)) {
        bool rhythm = isRhythm();
        for(int slotnum = 0; slotnum < 18; slotnum++) {
            slot.select(slotnum);

            // Updating rhythm status and key flag
            bool key = false;
            if (rhythm && slotnum >= 12) {
                switch (slotnum) {
                    case 12: //BD1
                    case 13: //BD2
                        key = (reg_flags >> 4) & 1;
                        break;
                    case 14: // HH
                        key = (reg_flags >> 0) & 1;
                        break;
                    case 15: // SD
                        key = (reg_flags >> 3) & 1;
                        break;
                    case 16: // TOM
                        key = (reg_flags >> 2) & 1;
                        break;
                    case 17: // CYM
                        key = (reg_flags >> 1) & 1;
                        break;
                    default:
                        break;
                }
            }
            if (reg_key[slotnum/2]) {
                key = true;
            }


            // calculate key-scale attenuation amount (controller.vhd)
            int ch = slotnum / 2;
            uint16_t fnum = slot.sd->pg_freq & 0x1ff; // 9 bits, F-Number
            uint8_t blk = slot.sd->pg_freq >> 9; // 3 bits, Block

            uint8_t kll = ( kl_table[(fnum >> 5) & 15] - ((7 - blk) << 3) ) << 1;
            
            if ((kll >> 7) || slot.patch.pd->_kl == 0) {
                kll = 0;
            }else{
                kll = kll >> (3 - slot.patch.pd->_kl);
            }
            
            // calculate base total level from volume register value
            uint8_t tll;
            if (rhythm && (slotnum == 14 || slotnum == 16)) { // hh and tom
                tll = reg_patch[ch] << 3;
            }else
            if ((slotnum & 1) == 0) {
                tll = slot.patch.pd->_tl << 1; // mod
            }else{
                tll = reg_volume[ch] << 3;     // car
            }
            
            tll = tll + kll;

            uint8_t tl;
            if ((tll >> 7) != 0) {
                tl = 0x7f;
            }else{
                tl = tll & 0x7f;
            }

            // output release rate (depends on the sustine and envelope type).
            uint8_t rr;  // 4 bits - Release Rate
            if  (key) { // key on
                if (slot.patch.pd->EG) {
                    rr = 0;
                }else{
                    rr = slot.patch.pd->RR;
                }
            }else{ // key off
                if ((slotnum & 1) == 0 && !(rhythm && ch >= 7)) {
                    rr  = 0;
                }else
                if (slot.sd->sustain) {
                    rr  = 5;
                }else
                if (!slot.patch.pd->EG) {
                    rr  = 7;
                }else{
                    rr  = slot.patch.pd->RR;
                }
            }

            // EnvelopeGenerator
            uint16_t egout;
            slot.vm2413EnvelopeGenerator(tl, rr, key, rhythm, egout);

            // PhaseGenerator
            bool noise;
            uint32_t pgout; // 18 bits
            slot.vm2413PhaseGenerator(key, rhythm, noise, pgout);

            // Operator
            uint16_t opout;
            slot.vm2413Operator(rhythm, noise, pgout, egout, opout);

            // OutputGenerator
            slot.vm2413OutputGenerator(opout);
        }

        // Music channels
        for (int i = 0; i < ((rhythm)? 6:9); i++) {
            bufs[i][sample] += narrow_cast<float>(slot.vm2413GetOutput(i*2+1));
        }
        // Drum channels
        if (rhythm) {
            bufs[6] = nullptr;
            bufs[7] = nullptr;
            bufs[8] = nullptr;

            bufs[9][sample] += narrow_cast<float>(slot.vm2413GetOutput(13) * 2); // BD
            bufs[10][sample] += narrow_cast<float>(slot.vm2413GetOutput(14) * 2); // HH
            bufs[11][sample] += narrow_cast<float>(slot.vm2413GetOutput(15) * 2); // SD
            bufs[12][sample] += narrow_cast<float>(slot.vm2413GetOutput(16) * 2); // TOM
            bufs[13][sample] += narrow_cast<float>(slot.vm2413GetOutput(17) * 2); // CYM
        }else{
            bufs[9] = nullptr;
            bufs[10] = nullptr;
            bufs[11] = nullptr;
            bufs[12] = nullptr;
            bufs[13] = nullptr;
        }
    }
}

void YM2413::generateChannels(std::span<float*, 9 + 5> bufs, unsigned num)
{
    assert(num != 0);

#if 1
    generateChannelsVM2413(bufs, num);
#else
    for (auto i : xrange(isRhythm() ? 6 : 9)) {
        Channel& ch = channels[i];
        slot.select(ch.car);
        if (slot.isActive()) {
            bool carFixedEnv = slot.sd->eg_state == one_of(SUSHOLD, FINISH);
            bool carSettle = slot.sd->eg_state == SETTLE;
            uint8_t carAMPM = slot.patch.pd->AMPM;
            slot.select(ch.mod);
            bool modFixedEnv = carSettle? false : slot.sd->eg_state == one_of(SUSHOLD, FINISH);
            uint8_t modAMPM = slot.patch.pd->AMPM;
            uint8_t modFB = slot.patch.pd->FB;
            unsigned flags = (carAMPM << 0) |
                             (modAMPM << 2) |
                             ((modFB != 0) << 4) |
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

        slot.select(ch6.car);
        bool ch6_car_active = slot.isActive();
        if (!ch6_car_active) {
            bufs[9] = nullptr;
        }
        slot.select(ch7.car);
        bool ch7_car_active = slot.isActive();
        if (!ch7_car_active) {
            bufs[10] = nullptr;
        }
        slot.select(ch8.car);
        bool ch8_car_active = slot.isActive();
        if (!ch8_car_active) {
            bufs[11] = nullptr;
        }
        slot.select(ch7.mod);
        bool ch7_mod_active = slot.isActive();
        if (!ch7_mod_active) {
            bufs[12] = nullptr;
        }
        slot.select(ch8.mod);
        bool ch8_mod_active = slot.isActive();
        if (!ch8_mod_active) {
            bufs[13] = nullptr;
        }

        for (auto sample : xrange(num)) {
            // BD
            if (ch6_car_active) {
                slot.select(ch6.mod);
                int calc_mod = slot.calc_slot_mod(false, false, false, 0, 0, 0);
                slot.select(ch6.car);
                int calc_car = slot.calc_slot_car(false, false, 0, 0, calc_mod, 0);
                bufs[9][sample] += narrow_cast<float>(2 * calc_car);
            }

            // Noise generator for SD and HH
            bool noise_bit = false;
            if (ch7_mod_active || ch7_car_active) {
                noise_seed >>= 1;
                noise_bit = noise_seed & 1;
                if (noise_bit) noise_seed ^= 0x8003020;
            }

            // SD
            if (ch7_car_active) {
                slot.select(ch7.car);
                bufs[10][sample] += narrow_cast<float>(
                    -2 * slot.calc_slot_snare(noise_bit));
            }

            // ch7_mod and ch8_car are used for both CYM and HH
            unsigned phase7 = 0, phase8 = 0;
            if (ch7_mod_active || ch8_car_active) {
                slot.select(ch7.mod);
                phase7 = slot.calc_phase(0);
                slot.select(ch8.car);
                phase8 = slot.calc_phase(0);
            }

            // CYM
            if (ch8_car_active) {
                bufs[11][sample] += narrow_cast<float>(
                    -2 * slot.calc_slot_cym(phase7, phase8));
            }

            // HH
            if (ch7_mod_active) {
                slot.select(ch7.mod);
                bufs[12][sample] += narrow_cast<float>(
                    2 * slot.calc_slot_hat(phase7, phase8, noise_bit));
            }

            // TOM
            if (ch8_mod_active) {
                slot.select(ch8.mod);
                bufs[13][sample] += narrow_cast<float>(
                    2 * slot.calc_slot_tom());
            }
        }
    }
    else {
        bufs[9] = nullptr;
        bufs[10] = nullptr;
        bufs[11] = nullptr;
        bufs[12] = nullptr;
        bufs[13] = nullptr;
    }
#endif
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

void YM2413::writePatchReg(uint8_t r, uint8_t data)
{
    assert(r < 8);

    switch (r) {
    case 0x00: {
        patch.select(getPatch(0, false));
        patch.pd->AMPM = (data >> 6) & 3;
        patch.pd->EG = (data >> 5) & 1;
        patch.setKR((data >> 4) & 1);
        patch.setML((data >> 0) & 15);
        break;
    }
    case 0x01: {
        patch.select(getPatch(0, true));
        patch.pd->AMPM = (data >> 6) & 3;
        patch.pd->EG = (data >> 5) & 1;
        patch.setKR((data >> 4) & 1);
        patch.setML((data >> 0) & 15);
        break;
    }
    case 0x02: {
        patch.select(getPatch(0, false));
        patch.setKL((data >> 6) & 3);
        patch.setTL((data >> 0) & 63);
        break;
    }
    case 0x03: {
        patch.select(getPatch(0, true));
        patch.setKL((data >> 6) & 3);
        patch.setWF((data >> 4) & 1);
        patch.select(getPatch(0, false));
        patch.setWF((data >> 3) & 1);
        patch.setFB((data >> 0) & 7);
        break;
    }
    case 0x04: {
        patch.select(getPatch(0, false));
        patch.pd->AR = (data >> 4) & 15;
        patch.pd->DR = (data >> 0) & 15;
        break;
    }
    case 0x05: {
        patch.select(getPatch(0, true));
        patch.pd->AR = (data >> 4) & 15;
        patch.pd->DR = (data >> 0) & 15;
        break;
    }
    case 0x06: {
        patch.select(getPatch(0, false));
        patch.setSL((data >> 4) & 15);
        patch.pd->RR = (data >> 0) & 15;
        break;
    }
    case 0x07: {
        patch.select(getPatch(0, true));
        patch.setSL((data >> 4) & 15);
        patch.pd->RR = (data >> 0) & 15;
        break;
    }
    default:
        break;
    }
}

void YM2413::writeReg(uint8_t r, uint8_t data)
{
    assert(r < 0x40);

    if (r < 8) {
        reg_instr[r] = data;
        writePatchReg(r, data);
    }

    switch (r) {
    case 0x00: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                uint16_t freq = getFreq(i);
                slot.select(ch.mod);
                slot.updatePG(freq);
                slot.updateRKS(freq);
                slot.updateEG();
            }
        }
        break;
    }
    case 0x01: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                uint16_t freq = getFreq(i);
                slot.select(ch.car);
                slot.updatePG(freq);
                slot.updateRKS(freq);
                slot.updateEG();
            }
        }
        break;
    }
    case 0x02: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                bool actAsCarrier = (i >= 7) && isRhythm();
                assert(!actAsCarrier); (void)actAsCarrier;
                slot.select(ch.mod);
                slot.updateTLL(getFreq(i), false);
            }
        }
        break;
    }
    case 0x03: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
            }
        }
        break;
    }
    case 0x04: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                slot.select(ch.mod);
                slot.updateEG();
                if (slot.sd->eg_state == ATTACK) {
                    slot.setEnvelopeState(ATTACK);
                }
            }
        }
        break;
    }
    case 0x05: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                slot.select(ch.car);
                slot.updateEG();
                if (slot.sd->eg_state == ATTACK) {
                    slot.setEnvelopeState(ATTACK);
                }
            }
        }
        break;
    }
    case 0x06: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                slot.select(ch.mod);
                slot.updateEG();
                if (slot.sd->eg_state == DECAY) {
                    slot.setEnvelopeState(DECAY);
                }
            }
        }
        break;
    }
    case 0x07: {
        for (auto i : xrange(isRhythm() ? 6 : 9)) {
            if (reg_patch[i] == 0) {
                Channel& ch = channels[i];
                ch.setPatch(getPatch(0, false), getPatch(0, true)); // TODO optimize
                slot.select(ch.car);
                slot.updateEG();
                if (slot.sd->eg_state == DECAY) {
                    slot.setEnvelopeState(DECAY);
                }
            }
        }
        break;
    }
    case 0x0E: {
        setRhythmFlags(data);
        break;
    }
    case 0x19: case 0x1A: case 0x1B: case 0x1C:
    case 0x1D: case 0x1E: case 0x1F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: {
        unsigned cha = r & 0x0F; assert(cha < 9);
        reg_freq[cha] = (reg_freq[cha] & 0xff00) | data;
        Channel& ch = channels[cha];
        bool actAsCarrier = (cha >= 7) && isRhythm();
        uint16_t freq = getFreq(cha);
        slot.select(ch.mod);
        slot.updateAll(freq, actAsCarrier);
        slot.select(ch.car);
        slot.updateAll(freq, true);
        break;
    }
    case 0x29: case 0x2A: case 0x2B: case 0x2C:
    case 0x2D: case 0x2E: case 0x2F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
    case 0x25: case 0x26: case 0x27: case 0x28: {
        unsigned cha = r & 0x0F; assert(cha < 9);
        reg_freq[cha] = (reg_freq[cha] & 0x00ff) | ((data & 15) << 8);
        reg_key[cha] = (data >> 4) & 1;
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
        slot.select(ch.mod);
        slot.updateAll(freq, modActAsCarrier);
        slot.select(ch.car);
        slot.updateAll(freq, true);
        break;
    }
    case 0x39: case 0x3A: case 0x3B: case 0x3C:
    case 0x3D: case 0x3E: case 0x3F:
        r -= 9; // verified on real YM2413
        [[fallthrough]];
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
    case 0x35: case 0x36: case 0x37: case 0x38: {
        unsigned cha = r & 0x0F; assert(cha < 9);
        reg_patch[cha] = data >> 4;
        reg_volume[cha] = data & 15;
        Channel& ch = channels[cha];
        if (isRhythm() && (cha >= 6)) {
            if (cha > 6) {
                // channel 7 or 8 in rhythm mode
                slot.select(ch.mod);
                slot.setVolume(data >> 4);
            }
        }
        else {
            ch.setPatch(getPatch(data >> 4, false), getPatch(data >> 4, true));
        }
        slot.select(ch.car);
        slot.setVolume(data & 15);
        bool actAsCarrier = (cha >= 7) && isRhythm();
        uint16_t freq = getFreq(cha);
        slot.select(ch.mod);
        slot.updateAll(freq, actAsCarrier);
        slot.select(ch.car);
        slot.updateAll(freq, true);
        break;
    }
    default:
        break;
    }
}

uint8_t YM2413::peekReg(uint8_t /*r*/) const
{
    return 0xff; // The original YM2413 does not allow reading back registers
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
        ar.serialize("feedback", sd->feedback,
            "output", sd->output,
            "cphase", sd->pg_phase,
            "state", sd->eg_state,
            "eg_phase", sd->eg_phase,
            "sustain", sd->sustain);

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
        ar.serialize("registers_instr", reg_instr);
        ar.serialize("registers_freq", reg_freq);
        ar.serialize("registers_volume", reg_volume);
        ar.serialize("registers_patch", reg_patch);
        ar.serialize("registers_key", reg_key);
        ar.serialize("registers_flags", reg_flags);
        if (ar.versionBelow(version, 2)) ar.endTag("YM2413Core");

        // no need to serialize patches[]
        //   patches[0] is restored from registers, the others are read-only
        ar.serialize("channels", channels,
            "slots", slot,
            "pm_phase", pm_phase,
            "am_phase", am_phase,
            "noise_seed", noise_seed);

        if constexpr (Archive::IS_LOADER) {
            patch.select(getPatch(0, false));
            patch.initModulator(reg_instr);
            patch.select(getPatch(0, true));
            patch.initCarrier(reg_instr);
            for (auto [i, ch] : enumerate(channels)) {
                // restore patch
                unsigned p = ((i >= 6) && isRhythm())
                    ? unsigned(16 + (i - 6))
                    : reg_patch[i];
                ch.setPatch(getPatch(p, false), getPatch(p, true)); // before updateAll()
                // restore volume
                slot.select(ch.car);
                slot.setVolume(reg_volume[i]);
                if (isRhythm() && (i >= 7)) { // ch 7/8 rhythm
                    slot.select(ch.mod);
                    slot.setVolume(reg_patch[i]);
                }
                // sync various variables
                bool actAsCarrier = (i >= 7) && isRhythm();
                uint16_t freq = getFreq(unsigned(i));
                slot.select(ch.mod);
                slot.updateAll(freq, actAsCarrier);
                slot.select(ch.car);
                slot.updateAll(freq, true);
                slot.select(ch.mod);
                slot.setEnvelopeState(slot.sd->eg_state);
                slot.select(ch.car);
                slot.setEnvelopeState(slot.sd->eg_state);
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
