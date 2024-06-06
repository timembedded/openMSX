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

Slot::Slot(int slots)
{
    numSlotData = slots;
    slotData = new(SlotData[numSlotData]);
    for (int i = 0; i < numSlotData; i++) {
        select(i);
        reset();
    }
    select(0);
}

Slot::~Slot()
{
    delete[] slotData;
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
    0b0000000, 0b0000000, 0b0000000, 0b0000000, 0b0000000, 0b0000001, 0b0000001, 0b0000001,
    0b0000001, 0b0000001, 0b0000010, 0b0000010, 0b0000010, 0b0000010, 0b0000011, 0b0000011,
    0b0000011, 0b0000011, 0b0000100, 0b0000100, 0b0000100, 0b0000100, 0b0000100, 0b0000101,
    0b0000101, 0b0000101, 0b0000110, 0b0000110, 0b0000110, 0b0000110, 0b0000111, 0b0000111,
    0b0000111, 0b0000111, 0b0001000, 0b0001000, 0b0001000, 0b0001001, 0b0001001, 0b0001001,
    0b0001001, 0b0001010, 0b0001010, 0b0001010, 0b0001011, 0b0001011, 0b0001011, 0b0001100,
    0b0001100, 0b0001100, 0b0001101, 0b0001101, 0b0001101, 0b0001110, 0b0001110, 0b0001110,
    0b0001111, 0b0001111, 0b0001111, 0b0010000, 0b0010000, 0b0010001, 0b0010001, 0b0010001,
    0b0010010, 0b0010010, 0b0010011, 0b0010011, 0b0010100, 0b0010100, 0b0010101, 0b0010101,
    0b0010101, 0b0010110, 0b0010110, 0b0010111, 0b0010111, 0b0011000, 0b0011000, 0b0011001,
    0b0011010, 0b0011010, 0b0011011, 0b0011011, 0b0011100, 0b0011101, 0b0011101, 0b0011110,
    0b0011110, 0b0011111, 0b0100000, 0b0100001, 0b0100001, 0b0100010, 0b0100011, 0b0100100,
    0b0100100, 0b0100101, 0b0100110, 0b0100111, 0b0101000, 0b0101001, 0b0101010, 0b0101011,
    0b0101100, 0b0101101, 0b0101111, 0b0110000, 0b0110001, 0b0110011, 0b0110100, 0b0110110,
    0b0111000, 0b0111001, 0b0111011, 0b0111101, 0b1000000, 0b1000010, 0b1000101, 0b1001000,
    0b1001011, 0b1010000, 0b1010100, 0b1011010, 0b1100010, 0b1101100, 0b1110101, 0b1111111
};

int16_t Slot::attack_multiply(uint8_t i0, int8_t i1)
{
    return ((uint16_t)i0 * (int16_t)i1) >> 2; // return 14-bit signed, 8-bit integer, 6-bit decimal
}

uint16_t Slot::attack_table(uint32_t addr /* 22 bits */) // returns 13 bits
{
    uint8_t w_addr1;
    uint8_t w_addr2;
    int8_t w_sub;
    int16_t w_mul;
    uint16_t w_inter;

    w_addr1 = addr >> 15;
    w_addr2 = (w_addr1 < 0xef)? w_addr1 + 1 : 0xef;

    uint8_t ff_w   = (addr >> 7) & 0xff;
    uint8_t ff_d1  = ar_adjust_array[w_addr1];
    uint8_t ff_d2  = ar_adjust_array[w_addr2];

    w_sub = ff_d2 - ff_d1;

    w_mul = attack_multiply(ff_w, w_sub);

    w_inter = (ff_d1 << 6) + w_mul;

    assert(std::min(ff_d1, ff_d2) <= (w_inter >> 6));
    assert(std::max(ff_d1, ff_d2) >= (w_inter >> 6));

    return w_inter & 0x1fff;
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
        uint16_t &egout // 13 bits
    )
{
    // Variables
    uint8_t rm = 0; // 0-31

    // Noise generator
    vm2413env.ntable = ((vm2413env.ntable << 1) & 0x3ffff) | ((vm2413env.ntable >> 17) ^ ((vm2413env.ntable >> 14) & 1));
    
    // Amplitude oscillator ( -4.8dB to 0dB , 3.7Hz )
    vm2413env.amphase++;
    if ((vm2413env.amphase & 0xf8000) == 0xf8000) {
        vm2413env.amphase &= 0xffff;
    }
    
    uint16_t egtmp = 0; // 15 bits
    switch (sd->vm2413env.eg_state) {
        case SlotData::EGState::Attack:
            rm = ar;
            egtmp = (tll << 6) + attack_table(sd->vm2413env.eg_phase & 0x3fffff);
            break;
        case SlotData::EGState::Decay:
            rm = dr;
            egtmp = (tll << 6) + ((sd->vm2413env.eg_phase >> 9) & 0x1fff);
            break;
        case SlotData::EGState::Release:
            rm = rrr;
            egtmp = (tll << 6) + ((sd->vm2413env.eg_phase >> 9) & 0x1fff);
            break;
        case SlotData::EGState::Finish:
            egtmp = 0x1fff;
            break;
    }
    
    // SD and HH
    if ((vm2413env.ntable & 1) && (slot/2)==7 && rhythm) {
        egtmp = egtmp + 0x2000;
    }

    // Amplitude LFO
    if (am) {
        if (((vm2413env.amphase >> 19) & 1) == 0) {
            // For uphill
            egtmp = egtmp + ((vm2413env.amphase >> 9) & 0x3ff) - 0x40;
        }else{
            // For downhill
            egtmp = egtmp + 0x3c0 - ((vm2413env.amphase >> 9) & 0x3ff);
        }
    }

    // Generate output
    if ((egtmp >> 13) == 0) {
        egout = egtmp & 0x1fff;
    }else{
        egout = 0x1fff;
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
                sd->vm2413env.eg_dphase <<= (rm - 1);
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
            if ((sd->vm2413env.eg_phase >> (22-4)) >= sl) {
                sd->vm2413env.eg_state = SlotData::EGState::Release;
            }
            break;
        case SlotData::EGState::Release:
            if ((sd->vm2413env.eg_phase >> (22-4)) >= 15 ) {
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
        uint32_t &pgout // 18 bits
    )
{
    noise = vm2413phase.noise14 ^ vm2413phase.noise17;
    pgout = sd->vm2413phase.pg_phase;

    // Update pitch LFO counter when slot = 0 and stage = 0 (i.e. increment per 72 clocks)
    if (slot == 0) {
        vm2413phase.pmcount = (vm2413phase.pmcount + 1) & 0x1fff;
    }

    // Delta phase
    uint32_t dphase = ((((uint32_t)fnum * ml_table[ml]) << blk) >> 3 /* must be 2 */) & 0x3ffff; // TODO: ">> 3" corrects the tone frequency, why?

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
        sd->vm2413phase.pg_phase = sd->vm2413phase.pg_phase + dphase;
        //assert((sd->vm2413phase.pg_phase & ~0x3ffff) == 0); // check 18-bits
    }
    sd->vm2413phase.pg_lastkey = key;
}

//-----------------------------------------------------------------------------------------
//-- Operator
//-----------------------------------------------------------------------------------------

static const uint16_t sin_data[128] = {
    0b11111111111, 0b11001010000, 0b10101010001, 0b10010111100,
    0b10001010011, 0b10000000001, 0b01110111110, 0b01110000101,
    0b01101010101, 0b01100101001, 0b01100000011, 0b01011100000,
    0b01011000000, 0b01010100011, 0b01010001000, 0b01001101111,
    0b01001011000, 0b01001000010, 0b01000101101, 0b01000011010,
    0b01000000111, 0b00111110110, 0b00111100101, 0b00111010101,
    0b00111000110, 0b00110110111, 0b00110101001, 0b00110011100,
    0b00110001111, 0b00110000011, 0b00101110111, 0b00101101011,
    0b00101100000, 0b00101010110, 0b00101001011, 0b00101000001,
    0b00100111000, 0b00100101110, 0b00100100101, 0b00100011100,
    0b00100010100, 0b00100001011, 0b00100000011, 0b00011111011,
    0b00011110100, 0b00011101100, 0b00011100101, 0b00011011110,
    0b00011010111, 0b00011010001, 0b00011001010, 0b00011000100,
    0b00010111110, 0b00010111000, 0b00010110010, 0b00010101100,
    0b00010100111, 0b00010100001, 0b00010011100, 0b00010010111,
    0b00010010010, 0b00010001101, 0b00010001000, 0b00010000011,
    0b00001111111, 0b00001111010, 0b00001110110, 0b00001110010,
    0b00001101110, 0b00001101010, 0b00001100110, 0b00001100010,
    0b00001011110, 0b00001011010, 0b00001010111, 0b00001010011,
    0b00001010000, 0b00001001101, 0b00001001001, 0b00001000110,
    0b00001000011, 0b00001000000, 0b00000111101, 0b00000111011,
    0b00000111000, 0b00000110101, 0b00000110011, 0b00000110000,
    0b00000101110, 0b00000101011, 0b00000101001, 0b00000100111,
    0b00000100101, 0b00000100010, 0b00000100000, 0b00000011110,
    0b00000011101, 0b00000011011, 0b00000011001, 0b00000010111,
    0b00000010110, 0b00000010100, 0b00000010011, 0b00000010001,
    0b00000010000, 0b00000001110, 0b00000001101, 0b00000001100,
    0b00000001011, 0b00000001010, 0b00000001001, 0b00000001000,
    0b00000000111, 0b00000000110, 0b00000000101, 0b00000000100,
    0b00000000011, 0b00000000011, 0b00000000010, 0b00000000010,
    0b00000000001, 0b00000000001, 0b00000000000, 0b00000000000,
    0b00000000000, 0b00000000000, 0b00000000000, 0b00000000000
};

void Slot::vm2413SineTable(
    bool wf,
    uint32_t addr, // 18 bits, integer part 9bit, decimal part 9bit
    uint16_t &data // 14 bits, integer part 8bit, decimal part 6bit
    )
{
    uint16_t ff_data0;  // 11 bits, unsigned integer part 7bit, decimal part 4bit
    uint16_t ff_data1;  // 11 bits, unsigned integer part 7bit, decimal part 4bit
    uint16_t w_wf;      // 14 bits
    uint8_t w_xor;      // 7 bits
    uint8_t w_addr0;    // 7 bits
    uint8_t w_addr1;    // 7 bits
    uint8_t w_xaddr;    // 7 bits
    bool ff_sign;
    bool ff_wf;
    uint16_t ff_weight; // 9 bits
    int16_t w_sub;      // 12 bits, signed integer part 8bit, decimal part 4bit
    int16_t w_mul;      // 14 bits, signed integer part 8bit, decimal part 6bit
    uint16_t w_inter;   // 14 bits

    w_xor   = ((addr >> 16) & 1)? 0x7f : 0;
    w_xaddr = ((addr >> 9) & 0x7f) ^ w_xor;
    w_addr0 = w_xaddr;
    w_addr1 = (((addr >> 9) & 0x7f) == 0x7f)? (0x7f ^ w_xor) :  //  Dealing with parts where the waveform cycles
              ((((addr >> 9) & 0x7f) + 1) ^ w_xor);

    // waveform memory
    ff_data0 = sin_data[w_addr0];
    ff_data1 = sin_data[w_addr1];

    // Modification information delay (to match waveform memory read delay)
    ff_sign   = addr >> 17;
    ff_wf     = wf && (addr >> 17);
    ff_weight = addr & 0x1ff;

    //  Interpolation (*Don't worry about ff_sign as it will be 0 in places that cross the sign)
    //  o = i0 * (1 - k) + i1 * w = i0 - w * i0 + w * i1 = i0 + w * (i1 - i0)
    w_sub  = ff_data1 - ff_data0;
    w_mul  = ((uint32_t)ff_weight * w_sub) >> 7;

    // Subordinate 6bit (decimal part) is left to maintain calculation accuracy
    w_inter = (ff_data0 << 2) + w_mul;   //  <<2 is digit alignment

    assert(std::min(ff_data0, ff_data1) <= (w_inter >> 2));
    assert(std::max(ff_data0, ff_data1) >= (w_inter >> 2));

    w_wf    = ff_wf? 0x3fff : 0;

    data    = (ff_sign? 0x2000 : 0) | (w_inter & 0x1fff) | w_wf;
}


void Slot::vm2413Operator(
        bool rhythm,
        bool noise,
        bool wf,
        uint8_t fb,  // 3 bits , Feedback
        uint32_t pgout, // 18 bits
        uint16_t egout, // 13 bits
        uint16_t &opout  // 14 bits
    )
{
    SignedLiType fdata; // 9 bits
    uint32_t addr; // 18 bits
    uint16_t data; // 14 bits
    bool w_is_carrier;
    uint32_t w_modula_m; // 20 bits
    uint32_t w_modula_c; // 20 bits
    uint32_t w_modula; // 20 bits

    // Get feedback data
    fdata = slotData[slot/2].fdata;

    w_is_carrier = slot & 1;
    w_modula_m   = (fb == 0)? 0 : ((fdata.value << 10) >> (fb ^ 7));
    w_modula_c   = fdata.value << 11;
    w_modula     = (w_is_carrier)? w_modula_c : w_modula_m;
    
    // Determine reference address (phase) of sine wave
    if (rhythm && (slot == 14 || slot == 17 )) { // HH or CYM
        addr = (noise)? 0x0fe00 : 0x2fe00;
    }else
    if (rhythm && slot == 15) { // SD
        addr = (pgout >> 17)? 0x0fe00 : 0x2fe00;
    }else
    if (rhythm && slot == 16) { // TOM
        addr = pgout;
    }else{
        if (!fdata.sign) {     // modula ? fdata. Since it is a value obtained by shifting the absolute value of , the sign is processed here.
            addr = pgout + (w_modula & 0x3ffff);
        }else{
            addr = pgout - (w_modula & 0x3ffff);
        }
    }

    // SineTable
    vm2413SineTable(wf, addr, data);

    // The stage where data comes out from SineTable
    if ((egout + (data & 0x1fff)) < 0x2000) {
        opout = (data & ~0x1fff) | (egout + (data & 0x1fff));
    }else{
        opout = data | 0x1fff;
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
    uint32_t addr,        // 14 bits, integer part 8bit, decimal part 6bit
    SignedLiType &data
    )
{
    uint16_t ff_weight; // 6 bits
    uint16_t ff_data0;  // 9 bits
    uint16_t ff_data1;  // 9 bits
    uint8_t w_addr1;    // 7 bits
    int16_t w_sub;      // 10 bits
    int16_t w_mul;      // 10 bits
    uint16_t w_inter;   // 10 bits

    w_addr1 = (((addr >> 6) & 0x7f) != 0x7f)? (((addr >> 6) + 1) & 0x7f) : 0x7f;

    // waveform memory
    ff_data0 = log2lin_data[(addr >> 6) & 0x7f];
    ff_data1 = log2lin_data[w_addr1];

    // Modification information delay (to match waveform memory read delay)
    data.sign = addr >> 13;
    ff_weight = addr & 0x3f;

    //  Interpolation (*Don't worry about ff_sign as it will be 0 in places that cross the sign)
    //  o = i0 * (1 - k) + i1 * w = i0 - w * i0 + w * i1 = i0 + w * (i1 - i0)
    w_sub  = ff_data1 - ff_data0;
    w_mul  = ((uint32_t)ff_weight * w_sub) >> 6;

    // Subordinate 6bit (decimal part) is left to maintain calculation accuracy
    w_inter = ff_data0 + w_mul;

    data.value = w_inter & 0x3ff;

    assert(std::min(ff_data0, ff_data1) <= data.value);
    assert(std::max(ff_data0, ff_data1) >= data.value);
}

//-----------------------------------------------------------------------------------------
//-- OutputGenerator
//-----------------------------------------------------------------------------------------

void Slot::vm2413OutputAverage(SignedLiType L, SignedLiType R, SignedLiType &OUT)
{
    uint16_t vL, vR; // 11 bits

    //  Sign + absolute value --> 2's complement
    if (!L.sign) {
        vL = L.value;
    }else{
        vL = (~L.value & 0x7ff) + 1;
    }
    if (!R.sign) {
        vR = R.value;
    }else{
        vR = (~R.value & 0x7ff) + 1;
    }

    vL = vL + vR;

    //  Two's complement ? sign + absolute value, and 1/2 times. One bit is lost here
    if ((vL & 0x400) == 0) { // positive
        OUT.sign = false;
        OUT.value = (vL & 0x3ff) >> 1;
    }else{ // negative
        OUT.sign = true;
        OUT.value = (~(vL - 1) & 0x3ff) >> 1;
    }

    if (L.sign == R.sign) {
        assert(std::min(L.value, R.value) <= OUT.value);
        assert(std::max(L.value, R.value) >= OUT.value);
    }else{
        assert(std::max(L.value, R.value) >= OUT.value);
    }
}

void Slot::vm2413OutputGenerator(
        uint16_t opout  // 14 bits
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

#define MIXER_ADD_MO(out, sample) if (!sample.sign) out += (sample.value << 1); else out -= (sample.value << 1)
#define MIXER_ADD_RO(out, sample) if (!sample.sign) out += (sample.value << 2); else out -= (sample.value << 2)

void Slot::vm2413TemporalMixer(
        bool rhythm,
        uint16_t &mo, // 16 bits
        uint16_t &ro  // 16 bits
    )
{
    MIXER_ADD_MO(mo, slotData[1].li_data); // CH0
    MIXER_ADD_MO(mo, slotData[3].li_data); // CH1
    MIXER_ADD_MO(mo, slotData[5].li_data); // CH2
    MIXER_ADD_MO(mo, slotData[7].li_data); // CH3
    MIXER_ADD_MO(mo, slotData[9].li_data); // CH4
    MIXER_ADD_MO(mo, slotData[11].li_data); // CH5
    if (!rhythm) {
        MIXER_ADD_MO(mo, slotData[13].li_data); // CH6
        MIXER_ADD_MO(mo, slotData[15].li_data); // CH7
        MIXER_ADD_MO(mo, slotData[17].li_data); // CH8
    }else{
        MIXER_ADD_RO(ro, slotData[13].li_data); // BD
        MIXER_ADD_RO(ro, slotData[14].li_data); // HH
        MIXER_ADD_RO(ro, slotData[15].li_data); // SD
        MIXER_ADD_RO(ro, slotData[16].li_data); // TOM
        MIXER_ADD_RO(ro, slotData[17].li_data); // CYM
    }
}

} // namespace YM2413Tim
} // namespace openmsx
