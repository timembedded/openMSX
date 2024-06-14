/*
 * Based on:
 *    emu2413.c -- YM2413 emulator written by Mitsutaka Okazaki 2001
 * heavily rewritten to fit openMSX structure
 */

#include "YM2413TimSlot.hh"
#include "YM2413TimCommon.hh"
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
// Slot
//

Slot::Slot(int slots) :
    slotData(slots)
{
    numSlotData = slots;
    for (int i = 0; i < numSlotData; i++) {
        select(i);
        reset();
    }
    select(0);
}

Slot::~Slot()
{
}

void Slot::reset()
{
    sd->vm2413phase.pg_phase = 0;
    sd->vm2413phase.pg_lastkey = false;
    sd->li_data = {false, 0};
    sd->fdata = {false, 0};
    sd->vm2413env.eg_lastkey = false;
    sd->vm2413env.eg_state = SlotData::EGState::Finish;
    sd->vm2413env.eg_phase = 0;
    sd->vm2413env.eg_dphase = 0;
    vm2413env.ntable = 0x3ffff;
    vm2413env.amphase = 0;
}

void Slot::select(int num)
{
    assert(num < numSlotData);
    slot = num;
    sd = &slotData[num];
}

//-----------------------------------------------------------------------------------------
//-- Controller
//-----------------------------------------------------------------------------------------

void Slot::vm2413Controller(
        // In
        bool rhythm,
        uint8_t reg_flags,
        uint8_t reg_key,
        uint8_t reg_sustain,
        bool    eg,     // 0-1
        uint8_t rr,     // 0-15
        bool    kr,     // 0-1   key scale of rate
        uint16_t fnum,  // 9 bits, F-Number
        uint8_t blk,    // 3 bits, Block
        // Out
        bool &kflag,    // 1 bit, key
        uint8_t &rks,   // 4 bits - Rate-KeyScale
        uint8_t &rrr    // 4 bits - Release Rate
    )
{
    // Updating rhythm status and key flag
    kflag = false;
    if (rhythm && slot >= 12) {
        switch (slot) {
            case 12: //BD1
            case 13: //BD2
                kflag = (reg_flags >> 4) & 1;
                break;
            case 14: // HH
                kflag = (reg_flags >> 0) & 1;
                break;
            case 15: // SD
                kflag = (reg_flags >> 3) & 1;
                break;
            case 16: // TOM
                kflag = (reg_flags >> 2) & 1;
                break;
            case 17: // CYM
                kflag = (reg_flags >> 1) & 1;
                break;
            default:
                break;
        }
    }
    if (reg_key) {
        kflag = true;
    }

    // determine RKS (controller.vhd)
    if (rhythm && slot >= 14) {
      if (kr) {
        rks = 5;
      }else{
        rks = blk >> 1;
      }
    }else{
      if (kr) {
        rks = (blk << 1) | (fnum >> 8);
      }else{
        rks = blk >> 1;
      }
    }
    
    // output release rate (depends on the sustine and envelope type) (controller.vhd)
    if  (kflag) { // key on
        if (eg) {
            rrr = 0;
        }else{
            rrr = rr;
        }
    }else{ // key off
        if ((slot & 1) == 0 && !(rhythm && slot >= 14)) {
            rrr  = 0;
        }else
        if (reg_sustain) {
            rrr  = 5;
        }else
        if (!eg) {
            rrr  = 7;
        }else{
            rrr  = rr;
        }
    }
}

//-----------------------------------------------------------------------------------------
//-- Envelope Generator
//-----------------------------------------------------------------------------------------

static const uint8_t ar_adjust_array[128] = {
    0b1111111, 0b1111111, 0b1101100, 0b1100010, 0b1011010, 0b1010100, 0b1010000, 0b1001011,
    0b1001000, 0b1000101, 0b1000010, 0b1000000, 0b0111101, 0b0111011, 0b0111001, 0b0111000,
    0b0110110, 0b0110100, 0b0110011, 0b0110001, 0b0110000, 0b0101111, 0b0101101, 0b0101100,
    0b0101011, 0b0101010, 0b0101001, 0b0101000, 0b0100111, 0b0100110, 0b0100101, 0b0100100,
    0b0100100, 0b0100011, 0b0100010, 0b0100001, 0b0100001, 0b0100000, 0b0011111, 0b0011110,
    0b0011110, 0b0011101, 0b0011101, 0b0011100, 0b0011011, 0b0011011, 0b0011010, 0b0011010,
    0b0011001, 0b0011000, 0b0011000, 0b0010111, 0b0010111, 0b0010110, 0b0010110, 0b0010101,
    0b0010101, 0b0010101, 0b0010100, 0b0010100, 0b0010011, 0b0010011, 0b0010010, 0b0010010,
    0b0010001, 0b0010001, 0b0010001, 0b0010000, 0b0010000, 0b0001111, 0b0001111, 0b0001111,
    0b0001110, 0b0001110, 0b0001110, 0b0001101, 0b0001101, 0b0001101, 0b0001100, 0b0001100,
    0b0001100, 0b0001011, 0b0001011, 0b0001011, 0b0001010, 0b0001010, 0b0001010, 0b0001001,
    0b0001001, 0b0001001, 0b0001001, 0b0001000, 0b0001000, 0b0001000, 0b0000111, 0b0000111,
    0b0000111, 0b0000111, 0b0000110, 0b0000110, 0b0000110, 0b0000110, 0b0000101, 0b0000101,
    0b0000101, 0b0000100, 0b0000100, 0b0000100, 0b0000100, 0b0000100, 0b0000011, 0b0000011,
    0b0000011, 0b0000011, 0b0000010, 0b0000010, 0b0000010, 0b0000010, 0b0000001, 0b0000001,
    0b0000001, 0b0000001, 0b0000001, 0b0000000, 0b0000000, 0b0000000, 0b0000000, 0b0000000
};

uint8_t Slot::attack_table(uint8_t addr /* 7 bits */)
{
    return ar_adjust_array[0x7f - addr];
}

void Slot::vm2413EnvelopeGenerator(
        uint8_t tll, // 7 bits 
        uint8_t rks, // 7 bits 
        uint8_t rrr, // 4 bits - Release Rate
        uint8_t ar,  // 4 bits - Attack Rate
        uint8_t dr,  // 4 bits - Decay Rate
        uint8_t sl,  // 4 bits - Sustine Level
        bool am,
        bool key,
        bool rhythm,
        uint8_t &egout // 7 bits
    )
{
    // Variables
    uint8_t rm = 0; // 0-31

    // Noise generator (18 bits)
    vm2413env.ntable = ((vm2413env.ntable << 1) & 0x3ffff) | ((vm2413env.ntable >> 17) ^ ((vm2413env.ntable >> 14) & 1));
    
    // Amplitude oscillator ( -4.8dB to 0dB , 3.7Hz )
    vm2413env.amphase++; // 20 bits
    if ((vm2413env.amphase & 0xf8000) == 0xf8000) {
        vm2413env.amphase &= 0xffff;
    }
    
    uint16_t egtmp = 0; // 9 bits, eg_phase is 23 bits
    switch (sd->vm2413env.eg_state) {
        case SlotData::EGState::Attack:
            rm = ar;
            egtmp = tll + attack_table((sd->vm2413env.eg_phase >> 15) & 0x7f);
            break;
        case SlotData::EGState::Decay:
            rm = dr;
            egtmp = tll + ((sd->vm2413env.eg_phase >> 15) & 0x7f);
            break;
        case SlotData::EGState::Release:
            rm = rrr;
            egtmp = tll + ((sd->vm2413env.eg_phase >> 15) & 0x7f);
            break;
        case SlotData::EGState::Finish:
            egtmp = 0x7f;
            break;
    }
    
    // SD and HH
    if ((vm2413env.ntable & 1) && (slot/2)==7 && rhythm) {
        egtmp = egtmp + 0x80;
    }

    // Amplitude LFO, amphase is 20 bits
    if (am) {
        if (((vm2413env.amphase >> 19) & 1) == 0) {
            // For uphill
            egtmp = egtmp + (((vm2413env.amphase >> 15) - 1) & 0x0f);
        }else{
            // For downhill
            egtmp = egtmp + (0x0f - ((vm2413env.amphase >> 15) & 0x0f));
        }
    }

    // Generate output
    if (egtmp < 0x80) {
        egout = egtmp;
    }else{
        egout = 0x7f;
    }

    if (rm != 0) {
        rm += (rks >> 2);
        if (rm > 15) {
            rm = 15;
        }

        switch (sd->vm2413env.eg_state) {
            case SlotData::EGState::Attack:
                sd->vm2413env.eg_dphase = (6 * (4 + (rks & 3))) & 0x3f;
                sd->vm2413env.eg_dphase <<= rm;
                sd->vm2413env.eg_phase = (sd->vm2413env.eg_phase - sd->vm2413env.eg_dphase) & 0x7fffff;
                break;
            case SlotData::EGState::Decay:
            case SlotData::EGState::Release:
                sd->vm2413env.eg_dphase &= 7;
                sd->vm2413env.eg_dphase |= 4 + (rks & 3);
                sd->vm2413env.eg_dphase <<= ((rm - 1) & 0x0f);
                sd->vm2413env.eg_phase = (sd->vm2413env.eg_phase + sd->vm2413env.eg_dphase) & 0x7fffff;
                break;
            case SlotData::EGState::Finish:
                break;
        }

    }

    switch (sd->vm2413env.eg_state) {
        case SlotData::EGState::Attack:
            if ((sd->vm2413env.eg_phase >> 22) & 1) {
                sd->vm2413env.eg_phase = 0;
                sd->vm2413env.eg_state = SlotData::EGState::Decay;
            }
            break;
        case SlotData::EGState::Decay:
            if (((sd->vm2413env.eg_phase >> (22-4)) & 0x1f) >= sl) {
                sd->vm2413env.eg_state = SlotData::EGState::Release;
            }
            break;
        case SlotData::EGState::Release:
            if (((sd->vm2413env.eg_phase >> (22-4)) & 0x1f) >= 15 ) {
                sd->vm2413env.eg_state = SlotData::EGState::Finish;
            }
            break;
        case SlotData::EGState::Finish:
            sd->vm2413env.eg_phase = 0x7fffff;
            break;
    };

    if (!sd->vm2413env.eg_lastkey && key) {
        sd->vm2413env.eg_phase = 0x3fffff;
        sd->vm2413env.eg_state = SlotData::EGState::Attack;
    }else
    if (sd->vm2413env.eg_lastkey && !key && sd->vm2413env.eg_state != SlotData::EGState::Finish) {
        sd->vm2413env.eg_state = SlotData::EGState::Release;
    }
    sd->vm2413env.eg_lastkey = key;
}

//-----------------------------------------------------------------------------------------
//-- Phase Generator
//-----------------------------------------------------------------------------------------

static const uint8_t ml_table[16] = {
    0b00001, 0b00010, 0b00100, 0b00110, 0b01000, 0b01010, 0b01100, 0b01110,
    0b10000, 0b10010, 0b10100, 0b10100, 0b11000, 0b11000, 0b11110, 0b11110
};

static const uint64_t noise14_tbl = 0x8888888911111110;
static const uint8_t noise17_tbl = 0x0a;

void Slot::vm2413PhaseGenerator(
        bool pm,
        uint8_t ml, // 4 bits, Multiple
        uint8_t blk, // 3 bits, Block
        uint16_t fnum, // 9 bits, F-Number
        bool key, // 1 bit
        bool rhythm, // 1 bit
        bool &noise,
        uint16_t &pgout // 9 bits
    )
{
    noise = vm2413phase.noise14 ^ vm2413phase.noise17;
    pgout = sd->vm2413phase.pg_phase >> 9;

    // Update pitch LFO counter when slot = 0 and stage = 0 (i.e. increment per 72 clocks)
    if (slot == 0) { // pmcount is 13 bits
        vm2413phase.pmcount = (vm2413phase.pmcount + 1) & 0x1fff;
    }

    // Delta phase (18 bits)
    uint32_t dphase = ((((uint32_t)fnum * ml_table[ml]) << blk) >> 2) & 0x3ffff;

    if (pm) {
        switch (vm2413phase.pmcount >> 11) {
        case 1:
            dphase = dphase + (dphase >> 7);
            break;
        case 3:
            dphase = dphase - (dphase >> 7);
            break;
        default:
            break;
        }
    }

    // Update noise
    if (slot == 14) {
      vm2413phase.noise14 = (noise14_tbl >> ((sd->vm2413phase.pg_phase >> 10) & 0x3f)) & 1;
    }else
    if (slot == 17) {
      vm2413phase.noise17 = (noise17_tbl >> ((sd->vm2413phase.pg_phase >> 11) & 7)) & 1;
    }

    // Update Phase
    if (!sd->vm2413phase.pg_lastkey && key && (!rhythm || (slot != 14 && slot != 17))) {
        sd->vm2413phase.pg_phase = 0;
    }else{
        sd->vm2413phase.pg_phase = (sd->vm2413phase.pg_phase + dphase) & 0x3ffff;
    }
    sd->vm2413phase.pg_lastkey = key;
}

//-----------------------------------------------------------------------------------------
//-- Operator
//-----------------------------------------------------------------------------------------

static const uint16_t sin_data[128] = {
    0b1111111, 0b1100101, 0b1010101, 0b1001100,
    0b1000101, 0b1000000, 0b0111100, 0b0111000,
    0b0110101, 0b0110011, 0b0110000, 0b0101110,
    0b0101100, 0b0101010, 0b0101000, 0b0100111,
    0b0100101, 0b0100100, 0b0100011, 0b0100001,
    0b0100000, 0b0011111, 0b0011110, 0b0011101,
    0b0011100, 0b0011011, 0b0011010, 0b0011010,
    0b0011001, 0b0011000, 0b0010111, 0b0010110,
    0b0010110, 0b0010101, 0b0010100, 0b0010100,
    0b0010011, 0b0010011, 0b0010010, 0b0010001,
    0b0010001, 0b0010000, 0b0010000, 0b0001111,
    0b0001111, 0b0001110, 0b0001110, 0b0001110,
    0b0001101, 0b0001101, 0b0001100, 0b0001100,
    0b0001011, 0b0001011, 0b0001011, 0b0001010,
    0b0001010, 0b0001010, 0b0001001, 0b0001001,
    0b0001001, 0b0001000, 0b0001000, 0b0001000,
    0b0001000, 0b0000111, 0b0000111, 0b0000111,
    0b0000110, 0b0000110, 0b0000110, 0b0000110,
    0b0000101, 0b0000101, 0b0000101, 0b0000101,
    0b0000101, 0b0000100, 0b0000100, 0b0000100,
    0b0000100, 0b0000100, 0b0000011, 0b0000011,
    0b0000011, 0b0000011, 0b0000011, 0b0000011,
    0b0000010, 0b0000010, 0b0000010, 0b0000010,
    0b0000010, 0b0000010, 0b0000010, 0b0000001,
    0b0000001, 0b0000001, 0b0000001, 0b0000001,
    0b0000001, 0b0000001, 0b0000001, 0b0000001,
    0b0000001, 0b0000000, 0b0000000, 0b0000000,
    0b0000000, 0b0000000, 0b0000000, 0b0000000,
    0b0000000, 0b0000000, 0b0000000, 0b0000000,
    0b0000000, 0b0000000, 0b0000000, 0b0000000,
    0b0000000, 0b0000000, 0b0000000, 0b0000000,
    0b0000000, 0b0000000, 0b0000000, 0b0000000
};

void Slot::vm2413SineTable(
    bool wf,
    uint16_t addr,      // 9 bits
    SignedDbType &data  // 7 bits  + sign
    )
{
    assert(addr < 0x200);
    if (addr < 0x80) {
        data.sign = false;
        data.value = sin_data[addr];
    }else
    if (addr < 0x100) {
        data.sign = false;
        data.value = sin_data[0x100 - 1 - addr];
    }else
    if (addr < 0x180) {
        data.sign = true;
        if (!wf) {
            data.value = sin_data[addr - 0x100];
        }else{
            data.value = sin_data[0];
        }
    }else{
        data.sign = true;
        if (!wf) {
            data.value = sin_data[0x200 - 1 - addr];
        }else{
            data.value = sin_data[0];
        }
    }
}

void Slot::vm2413Operator(
        bool rhythm,
        bool noise,
        bool wf,
        uint8_t fb,  // 3 bits , Feedback
        uint16_t pgout, // 9 bits
        uint8_t egout, // 7 bits
        SignedDbType &opout  // 7 bits + sign
    )
{
    SignedLiType fdata; // 9 bits
    uint16_t addr; // 9 bits
    SignedDbType data; // 7 bits + sign

    // Get feedback data
    fdata = slotData[slot/2].fdata;

    // Determine reference address (phase) of sine wave
    if (rhythm && (slot == 14 || slot == 17)) { // HH or CYM
        addr = (noise)? 0x7f : 0x17f;
    }else
    if (rhythm && slot == 15) { // SD
        addr = (pgout >> 8)? 0x7f : 0x17f;
    }else
    if (rhythm && slot == 16) { // TOM
        addr = pgout;
    }else{
        uint16_t modula; // 11 bits
        if (slot & 1) {
            modula = fdata.value << 2;
        }else{
            if (fb == 0) {
                modula = 0;
            }else{
                modula = (fdata.value << 1) >> (7 - fb);
            }
        }

        if (!fdata.sign) {
            addr = (pgout + modula) & 0x1ff;
        }else{
            addr = (pgout - modula) & 0x1ff;
        }
    }

    // SineTable
    vm2413SineTable(wf, addr, data);

    // The stage where data comes out from SineTable
    uint8_t opout_buf = egout + data.value; // 8-bit
    opout.sign = data.sign;
    if (opout_buf < 0x80) {
        opout.value = opout_buf;
    }else{
        opout.value = 0x7f;
    }
}

//-----------------------------------------------------------------------------------------
//-- LinearTable
//-----------------------------------------------------------------------------------------

static const uint16_t log2lin_data[128] = {
    0b111111111, 0b111101001, 0b111010100, 0b111000000,
    0b110101101, 0b110011011, 0b110001010, 0b101111001,
    0b101101001, 0b101011010, 0b101001011, 0b100111101,
    0b100110000, 0b100100011, 0b100010111, 0b100001011,
    0b100000000, 0b011110101, 0b011101010, 0b011100000,
    0b011010111, 0b011001110, 0b011000101, 0b010111101,
    0b010110101, 0b010101101, 0b010100110, 0b010011111,
    0b010011000, 0b010010010, 0b010001011, 0b010000110,
    0b010000000, 0b001111010, 0b001110101, 0b001110000,
    0b001101011, 0b001100111, 0b001100011, 0b001011110,
    0b001011010, 0b001010111, 0b001010011, 0b001001111,
    0b001001100, 0b001001001, 0b001000110, 0b001000011,
    0b001000000, 0b000111101, 0b000111011, 0b000111000,
    0b000110110, 0b000110011, 0b000110001, 0b000101111,
    0b000101101, 0b000101011, 0b000101001, 0b000101000,
    0b000100110, 0b000100100, 0b000100011, 0b000100001,
    0b000100000, 0b000011110, 0b000011101, 0b000011100,
    0b000011011, 0b000011001, 0b000011000, 0b000010111,
    0b000010110, 0b000010101, 0b000010100, 0b000010100,
    0b000010011, 0b000010010, 0b000010001, 0b000010000,
    0b000010000, 0b000001111, 0b000001110, 0b000001110,
    0b000001101, 0b000001101, 0b000001100, 0b000001011,
    0b000001011, 0b000001010, 0b000001010, 0b000001010,
    0b000001001, 0b000001001, 0b000001000, 0b000001000,
    0b000001000, 0b000000111, 0b000000111, 0b000000111,
    0b000000110, 0b000000110, 0b000000110, 0b000000101,
    0b000000101, 0b000000101, 0b000000101, 0b000000101,
    0b000000100, 0b000000100, 0b000000100, 0b000000100,
    0b000000100, 0b000000011, 0b000000011, 0b000000011,
    0b000000011, 0b000000011, 0b000000011, 0b000000011,
    0b000000010, 0b000000010, 0b000000010, 0b000000010,
    0b000000010, 0b000000010, 0b000000010, 0b000000000
};

void Slot::vm2413LinearTable(
    SignedDbType addr,
    SignedLiType &data
    )
{
    data.sign = addr.sign;
    data.value = log2lin_data[addr.value];
}

//-----------------------------------------------------------------------------------------
//-- OutputGenerator
//-----------------------------------------------------------------------------------------

void Slot::vm2413OutputAverage(SignedLiType L, SignedLiType R, SignedLiType &OUT)
{
    if (L.sign == R.sign) {
        OUT.sign = L.sign;
        OUT.value = (L.value + R.value) >> 1;
    }
    else {
        if (L.value > R.value) {
            OUT.sign = L.sign;
            OUT.value = (L.value - R.value) >> 1;
        }
        else {
            OUT.sign = R.sign;
            OUT.value = (R.value - L.value) >> 1;

        }
    }

    if (L.sign == R.sign) {
        assert(std::min(L.value, R.value) <= OUT.value);
        assert(std::max(L.value, R.value) >= OUT.value);
    }else{
        assert(std::max(L.value, R.value) >= OUT.value);
    }
}

void Slot::vm2413OutputGenerator(
        SignedDbType opout  // 7 bits + sign
    )
{
    SignedLiType li_data;

    vm2413LinearTable(opout, li_data);

    if ((slot & 1) == 0) {
        // Write to feedback memory only when it is a modulator
        vm2413OutputAverage(sd->li_data, li_data, slotData[slot/2].fdata);
    }
    // Store raw output
    sd->li_data = li_data;
}

//-----------------------------------------------------------------------------------------
//-- TemporalMixer
//-----------------------------------------------------------------------------------------

int Slot::vm2413GetOutput(int slotnum)
{
    SignedLiType li = slotData[slotnum].li_data;
    if (li.sign) {
        return -li.value;
    }else{
        return li.value;
    }
}

#define MIXER_ADD(out, sample) if (!sample.sign) out += (sample.value << 1); else out -= (sample.value << 1)

void Slot::vm2413TemporalMixer(
        bool rhythm,
        uint16_t &mo, // 16 bits
        uint16_t &ro  // 16 bits
    )
{
    MIXER_ADD(mo, slotData[1].li_data); // CH0
    MIXER_ADD(mo, slotData[3].li_data); // CH1
    MIXER_ADD(mo, slotData[5].li_data); // CH2
    MIXER_ADD(mo, slotData[7].li_data); // CH3
    MIXER_ADD(mo, slotData[9].li_data); // CH4
    MIXER_ADD(mo, slotData[11].li_data); // CH5
    if (!rhythm) {
        MIXER_ADD(mo, slotData[13].li_data); // CH6
        MIXER_ADD(mo, slotData[15].li_data); // CH7
        MIXER_ADD(mo, slotData[17].li_data); // CH8
    }else{
        MIXER_ADD(ro, slotData[13].li_data); // BD
        MIXER_ADD(ro, slotData[14].li_data); // HH
        MIXER_ADD(ro, slotData[15].li_data); // SD
        MIXER_ADD(ro, slotData[16].li_data); // TOM
        MIXER_ADD(ro, slotData[17].li_data); // CYM
    }
}

} // namespace YM2413Tim
} // namespace openmsx
