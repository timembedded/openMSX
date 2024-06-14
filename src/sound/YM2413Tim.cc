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
    slot(18)
{
    ranges::fill(reg_instr, 0);
    ranges::fill(reg_freq, 0);
    ranges::fill(reg_patch, 0);
    ranges::fill(reg_volume, 0);
    ranges::fill(reg_key, 0);
    ranges::fill(reg_sustain, 0);

    for (auto i : xrange(16 + 3)) {
        patch[i<<1].reset();
        patch[i<<1].initModulator(inst_data[i]);
        patch[(i<<1)+1].reset();
        patch[(i<<1)+1].initCarrier(inst_data[i]);
    }

    // Fix for error in voice data for SD
    patch[35].ml = 2;

    reset();
}

// Reset whole of OPLL except patch data
void YM2413::reset()
{
    pm_phase = 0;
    am_phase = 0;
    noise_seed = 0xFFFF;

    for (auto i : xrange(uint8_t(0x40))) {
        writeReg(i, 0);
    }
    registerLatch = 0;
}

void YM2413::setRhythmFlags(uint8_t flags)
{
    reg_flags = flags;
}

float YM2413::getAmplificationFactor() const
{
    return 1.0f / (1 << DB2LIN_AMP_BITS);
}

bool YM2413::isRhythm() const
{
    return (reg_flags & 0x20) != 0;
}

int YM2413::getPatch(unsigned instrument, bool carrier)
{
    return (instrument << 1) + (carrier? 1:0);
}

static const uint8_t kl_table[16] = {
    0b000000, 0b011000, 0b100000, 0b100101,
    0b101000, 0b101011, 0b101101, 0b101111,
    0b110000, 0b110010, 0b110011, 0b110100,
    0b110101, 0b110110, 0b110111, 0b111000
}; // 0.75db/step, 6db/oct

void YM2413::generateChannels(std::span<float*, 9 + 5> bufs, unsigned num)
{
    assert(num != 0);

    for (auto sample : xrange(num)) {
        bool rhythm = isRhythm();
        for(int slotnum = 0; slotnum < 18; slotnum++) {
            slot.select(slotnum);

            int cha = slotnum / 2;

            // select instrument
            Patch *pat;
            if (rhythm && slotnum >= 12) {
                pat = &patch[slotnum - 12 + 32];
            }else{
                pat = &patch[reg_patch[cha]*2 + (slotnum & 1)];
            }

            // Controller
            // ----------

            uint8_t kl = pat->kl;   // 0-3   key scale level
            bool    eg = pat->eg;   // 0-1
            uint8_t tl = pat->tl;   // 0-63  volume (total level)
            uint8_t rr = pat->rr;   // 0-15
            bool    kr = pat->kr;   // 0-1   key scale of rate

            bool kflag;
            uint16_t fnum;
            uint8_t blk;  // 3 bits, Block
            uint8_t kll;
            uint8_t tll;
            uint8_t rks;  // 4 bits - Rate-KeyScale
            uint8_t rrr;  // 4 bits - Release Rate

            // calculate key-scale attenuation amount (controller.vhd)
            fnum = reg_freq[cha] & 0x1ff; // 9 bits, F-Number
            blk = (reg_freq[cha] >> 9) & 7; // 3 bits, Block
            
            kll = ( kl_table[(fnum >> 5) & 15] - ((7 - blk) << 3) ) << 1;
            
            if ((kll >> 7) || kl == 0) {
                kll = 0;
            }else{
                kll = kll >> (3 - kl);
            }
            
            // calculate base total level from volume register value (controller.vhd)
            if (rhythm && (slotnum == 14 || slotnum == 16)) { // hh and tom
                tll = reg_patch[cha] << 3;
            }else
            if ((slotnum & 1) == 0) {
                tll = tl << 1; // mod
            }else{
                tll = reg_volume[cha] << 3; // car
            }
            
            tll = tll + kll;
            
            if ((tll >> 7) != 0) {
                tll = 0x7f;
            }else{
                tll = tll & 0x7f;
            }

            slot.vm2413Controller(rhythm, reg_flags, reg_key[cha], reg_sustain[cha],
                eg, rr, kr, fnum, blk, kflag, rks, rrr);


            // EnvelopeGenerator
            // -----------------

            uint8_t ar  = pat->ar;  // 0-15  attack rate
            uint8_t dr  = pat->dr;  // 0-15  decay rate
            uint8_t sl  = pat->sl;  // 0-15  sustain level
            bool am = pat->am;      // 0-1
            uint8_t egout;          // output 7 bits
            slot.vm2413EnvelopeGenerator(tll, rks, rrr, ar, dr, sl, am, kflag, rhythm, egout);

            // PhaseGenerator
            // --------------

            bool pm = pat->pm;      // 0-1
            uint8_t ml = pat->ml;   // 0-15  frequency multiplier factor
            bool noise;
            uint16_t pgout; // 9 bits
            slot.vm2413PhaseGenerator(pm, ml, blk, fnum, kflag, rhythm, noise, pgout);

            // Operator
            // --------

            bool wf = pat->wf;      // 0-1   waveform
            uint8_t fb = pat->fb;   // 0,1-7 amount of feedback
            Slot::SignedDbType opout;
            slot.vm2413Operator(rhythm, noise, wf, fb, pgout, egout, opout);

            // OutputGenerator
            // ---------------

            slot.vm2413OutputGenerator(opout);
        }

        // Music channels
        for (int i = 0; i < ((rhythm)? 6:9); i++) {
            bufs[i][sample] += narrow_cast<float>(slot.vm2413GetOutput(i*2+1)) / 2;
        }
        // Drum channels
        if (rhythm) {
            bufs[6] = nullptr;
            bufs[7] = nullptr;
            bufs[8] = nullptr;

            bufs[9][sample]  += narrow_cast<float>(slot.vm2413GetOutput(13)); // BD
            bufs[10][sample] -= narrow_cast<float>(slot.vm2413GetOutput(15)); // SD
            bufs[11][sample] -= narrow_cast<float>(slot.vm2413GetOutput(17)); // CYM
            bufs[12][sample] += narrow_cast<float>(slot.vm2413GetOutput(14)); // HH
            bufs[13][sample] += narrow_cast<float>(slot.vm2413GetOutput(16)); // TOM
        }else{
            bufs[9]  = nullptr;
            bufs[10] = nullptr;
            bufs[11] = nullptr;
            bufs[12] = nullptr;
            bufs[13] = nullptr;
        }
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

void YM2413::writePatchReg(uint8_t r, uint8_t data)
{
    assert(r < 8);

    switch (r) {
    case 0x00: {
        patch[0].am = (data >> 7) & 1;
        patch[0].pm = (data >> 6) & 1;
        patch[0].eg = (data >> 5) & 1;
        patch[0].kr = (data >> 4) & 1;
        patch[0].ml = (data >> 0) & 15;
        break;
    }
    case 0x01: {
        patch[1].am = (data >> 7) & 1;
        patch[1].pm = (data >> 6) & 1;
        patch[1].eg = (data >> 5) & 1;
        patch[1].kr = (data >> 4) & 1;
        patch[1].ml = (data >> 0) & 15;
        break;
    }
    case 0x02: {
        patch[0].kl = (data >> 6) & 3;
        patch[0].tl = (data >> 0) & 63;
        break;
    }
    case 0x03: {
        patch[1].kl = (data >> 6) & 3;
        patch[1].wf = (data >> 4) & 1;
        patch[0].wf = (data >> 3) & 1;
        patch[0].fb = (data >> 0) & 7;
        break;
    }
    case 0x04: {
        patch[0].ar = (data >> 4) & 15;
        patch[0].dr = (data >> 0) & 15;
        break;
    }
    case 0x05: {
        patch[1].ar = (data >> 4) & 15;
        patch[1].dr = (data >> 0) & 15;
        break;
    }
    case 0x06: {
        patch[0].sl = (data >> 4) & 15;
        patch[0].rr = (data >> 0) & 15;
        break;
    }
    case 0x07: {
        patch[1].sl = (data >> 4) & 15;
        patch[1].rr = (data >> 0) & 15;
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
        bool actAsCarrier = (cha >= 7) && isRhythm();
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
        reg_sustain[cha] = (data >> 5) & 1;
        bool modActAsCarrier = (cha >= 7) && isRhythm();
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

namespace YM2413Tim {

    // version 1: initial version
    // version 2: don't serialize "type / actAsCarrier" anymore, it's now
    //            a calculated value
    // version 3: don't serialize slot_on_flag anymore
    // version 4: don't serialize volume anymore
    template<typename Archive>
    void Slot::serialize(Archive& /*ar*/, unsigned /*version*/)
    {
/*
        ar.serialize(
            "vm2413env", sd->vm2413env,
            "fdata", sd->fdata,
            "vm2413phase", sd->vm2413phase,
            "li_data", sd->li_data);
*/
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
        ar.serialize("reg_sustain", reg_sustain);
        ar.serialize("registers_flags", reg_flags);
        if (ar.versionBelow(version, 2)) ar.endTag("YM2413Core");

        // no need to serialize patches[]
        //   patches[0] is restored from registers, the others are read-only
        ar.serialize(
            "slots", slot,
            "pm_phase", pm_phase,
            "am_phase", am_phase,
            "noise_seed", noise_seed);

        if constexpr (Archive::IS_LOADER) {
            patch[0].initModulator(reg_instr);
            patch[1].initCarrier(reg_instr);
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
