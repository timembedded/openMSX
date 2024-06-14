/*
  * Based on:
  *    emu8950.c -- Y8950 emulator written by Mitsutaka Okazaki 2001
  * heavily rewritten to fit openMSX structure
  */

// TODO: Rythm mode currently do no sound correct
// TODO: Frequency divder is different from YM2413 (1025 vs 512), now bit precission is lost

#include "Y8950Tim.hh"
#include "Y8950Periphery.hh"
#include "MSXAudio.hh"
#include "DeviceConfig.hh"
#include "MSXMotherBoard.hh"
#include "Math.hh"
#include "cstd.hh"
#include "enumerate.hh"
#include "narrow.hh"
#include "outer.hh"
#include "ranges.hh"
#include "serialize.hh"
#include "xrange.hh"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace openmsx {

static constexpr unsigned MOD = 0;
static constexpr unsigned CAR = 1;

// Bits for liner value
static constexpr int DB2LIN_AMP_BITS = 11;


//**************************************************//
//                                                  //
//  Helper functions                                //
//                                                  //
//**************************************************//


// class Y8950Tim::Patch

Y8950Tim::Patch::Patch()
{
    reset();
}

void Y8950Tim::Patch::reset()
{
    am = false;
    pm = false;
    eg = false;
    ml = 0;
    kl = 0;
    tl = 0;
    ar = 0;
    dr = 0;
    sl = 0;
    rr = 0;
    kr = false;
    fb = 0;
}

Y8950Tim::Channel::Channel()
{
    reset();
}

void Y8950Tim::Channel::reset()
{
    reg_key = 0;
    reg_freq = 0;
}


static constexpr auto INPUT_RATE = unsigned(cstd::round(Y8950Tim::CLOCK_FREQ / double(Y8950Tim::CLOCK_FREQ_DIV)));

Y8950Tim::Y8950Tim(const std::string& name_, const DeviceConfig& config,
             unsigned sampleRam, EmuTime::param time, MSXAudio& audio)
    : ResampledSoundDevice(config.getMotherBoard(), name_, "MSX-AUDIO", 9 + 5 + 1, INPUT_RATE, false)
    , motherBoard(config.getMotherBoard())
    , periphery(audio.createPeriphery(getName()))
    , adpcm(*this, config, name_, sampleRam)
    , connector(motherBoard.getPluggingController())
    , dac13(name_ + " DAC", "MSX-AUDIO 13-bit DAC", config)
    , debuggable(motherBoard, getName())
    , timer1(EmuTimer::createOPL3_1(motherBoard.getScheduler(), *this))
    , timer2(EmuTimer::createOPL3_2(motherBoard.getScheduler(), *this))
    , irq(motherBoard, getName() + ".IRQ")
    , slot(18)
{
    reset(time);
    registerSound(config);
}

Y8950Tim::~Y8950Tim()
{
    unregisterSound();
}

void Y8950Tim::clearRam()
{
    adpcm.clearRam();
}

// Reset whole of opl except patch data.
void Y8950Tim::reset(EmuTime::param time)
{
    rythm_mode = false;
    am_mode = false;
    pm_mode = false;

    // update the output buffer before changing the register
    updateStream(time);

    ranges::fill(reg, 0);

    for (auto i : xrange(9)) {
        channel[i].reset();
    }
    for (auto i : xrange(18)) {
        patch[i].reset();
    }

    reg[0x04] = 0x18;
    reg[0x19] = 0x0F; // fixes 'Thunderbirds are Go'
    status = 0x00;
    statusMask = 0;
    irq.reset();

    adpcm.reset(time);
}


float Y8950Tim::getAmplificationFactorImpl() const
{
    return 1.0f / (1 << DB2LIN_AMP_BITS);
}

void Y8950Tim::setEnabled(bool enabled_, EmuTime::param time)
{
    updateStream(time);
    enabled = enabled_;
}

static const uint8_t kl_table[16] = {
    0b000000, 0b011000, 0b100000, 0b100101,
    0b101000, 0b101011, 0b101101, 0b101111,
    0b110000, 0b110010, 0b110011, 0b110100,
    0b110101, 0b110110, 0b110111, 0b111000
}; // 0.75db/step, 6db/oct

void Y8950Tim::generateChannels(std::span<float*> bufs, unsigned num)
{
    for (auto sample : xrange(num)) {
        bool rhythm = rythm_mode;
        for(int slotnum = 0; slotnum < 18; slotnum++) {
            slot.select(slotnum);

            int cha = slotnum / 2;

            // select instrument
            Patch *pat;
            pat = &patch[slotnum];

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

            // TODO: Calculation of 'kll' and 'tll' is guesswork, might be incorrect

            fnum = (channel[cha].reg_freq & 0x3ff) >> 1; // 9 bits, F-Number
            blk = channel[cha].reg_freq >> 10; // 3 bits, Block

            kll = ( kl_table[(fnum >> 5) & 15] - ((7 - blk) << 3) ) << 1;
            
            if ((kll >> 7) || kl == 0) {
                kll = 0;
            }else{
                kll = kll >> (3 - kl);
            }
            
            // calculate base total level from volume register value (controller.vhd)
            tll = tl << 1; // mod
            tll = tll + kll;
            
            if ((tll >> 7) != 0) {
                tll = 0x7f;
            }else{
                tll = tll & 0x7f;
            }

            slot.vm2413Controller(rhythm, reg_flags, channel[cha].reg_key,
                0 /*reg_sustain[cha]*/, // TODO: What about sustain?
                eg, rr, kr, fnum, blk, kflag, rks, rrr);


            // EnvelopeGenerator
            // -----------------

            uint8_t ar  = pat->ar;  // 0-15  attack rate
            uint8_t dr  = pat->dr;  // 0-15  decay rate
            uint8_t sl  = pat->sl;  // 0-15  sustain level
            bool am = pat->am;      // 0-1
            uint8_t egout;
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

            bool wf = false;
            uint8_t fb = pat->fb;   // 0,1-7 amount of feedback
            YM2413Tim::Slot::SignedDbType opout;
            slot.vm2413Operator(rhythm, noise, wf, fb, pgout, egout, opout);

            // OutputGenerator
            // ---------------

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

//
// I/O Ctrl
//

void Y8950Tim::writeReg(uint8_t rg, uint8_t data, EmuTime::param time)
{
    static constexpr std::array<int, 32> sTbl = {
         0,  2,  4,  1,  3,  5, -1, -1,
         6,  8, 10,  7,  9, 11, -1, -1,
        12, 14, 16, 13, 15, 17, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1
    };

    // TODO only for registers that influence sound
    // TODO also ADPCM
    //if (rg >= 0x20) {
        // update the output buffer before changing the register
        updateStream(time);
    //}

    switch (rg & 0xe0) {
    case 0x00: {
        switch (rg) {
        case 0x01: // TEST
            // TODO
            // Y8950 MSX-AUDIO Test register $01 (write only)
            //
            // Bit Description
            //
            //  7  Reset LFOs - seems to force the LFOs to their initial
            //     values (eg. maximum amplitude, zero phase deviation)
            //
            //  6  something to do with ADPCM - bit 0 of the status
            //     register is affected by setting this bit (PCM BSY)
            //
            //  5  No effect? - Waveform select enable in YM3812 OPL2 so seems
            //     reasonable that this bit wouldn't have been used in OPL
            //
            //  4  No effect?
            //
            //  3  Faster LFOs - increases the frequencies of the LFOs and
            //     (maybe) the timers (cf. YM2151 test register)
            //
            //  2  Reset phase generators - No phase generator output, but
            //     envelope generators still work (can hear a transient
            //     when they are gated)
            //
            //  1  No effect?
            //
            //  0  Reset envelopes - Envelope generator outputs forced
            //     to maximum, so all enabled voices sound at maximum
            reg[rg] = data;
            break;

        case 0x02: // TIMER1 (resolution 80us)
            timer1->setValue(data);
            reg[rg] = data;
            break;

        case 0x03: // TIMER2 (resolution 320us)
            timer2->setValue(data);
            reg[rg] = data;
            break;

        case 0x04: // FLAG CONTROL
            if (data & Y8950Tim::R04_IRQ_RESET) {
                resetStatus(0x78);  // reset all flags
            } else {
                changeStatusMask((~data) & 0x78);
                timer1->setStart((data & Y8950Tim::R04_ST1) != 0, time);
                timer2->setStart((data & Y8950Tim::R04_ST2) != 0, time);
                reg[rg] = data;
            }
            adpcm.resetStatus();
            break;

        case 0x06: // (KEYBOARD OUT)
            connector.write(data, time);
            reg[rg] = data;
            break;

        case 0x07: // START/REC/MEM DATA/REPEAT/SP-OFF/-/-/RESET
            periphery.setSPOFF((data & 8) != 0, time); // bit 3
            [[fallthrough]];

        case 0x08: // CSM/KEY BOARD SPLIT/-/-/SAMPLE/DA AD/64K/ROM
        case 0x09: // START ADDRESS (L)
        case 0x0A: // START ADDRESS (H)
        case 0x0B: // STOP ADDRESS (L)
        case 0x0C: // STOP ADDRESS (H)
        case 0x0D: // PRESCALE (L)
        case 0x0E: // PRESCALE (H)
        case 0x0F: // ADPCM-DATA
        case 0x10: // DELTA-N (L)
        case 0x11: // DELTA-N (H)
        case 0x12: // ENVELOP CONTROL
        case 0x1A: // PCM-DATA
            reg[rg] = data;
            adpcm.writeReg(rg, data, time);
            break;

        case 0x15: // DAC-DATA  (bit9-2)
            reg[rg] = data;
            if (reg[0x08] & 0x04) {
                int tmp = static_cast<signed char>(reg[0x15]) * 256
                        + reg[0x16];
                tmp = (tmp * 4) >> (7 - reg[0x17]);
                dac13.writeDAC(Math::clipToInt16(tmp), time);
            }
            break;
        case 0x16: //           (bit1-0)
            reg[rg] = data & 0xC0;
            break;
        case 0x17: //           (exponent)
            reg[rg] = data & 0x07;
            break;

        case 0x18: // I/O-CONTROL (bit3-0)
            // 0 -> input
            // 1 -> output
            reg[rg] = data;
            periphery.write(reg[0x18], reg[0x19], time);
            break;

        case 0x19: // I/O-DATA (bit3-0)
            reg[rg] = data;
            periphery.write(reg[0x18], reg[0x19], time);
            break;
        }
        break;
    }
    case 0x20: {
        if (int s = sTbl[rg & 0x1f]; s >= 0) {
            Patch &p = patch[s];
            p.am = (data >> 7) &  1;
            p.pm = (data >> 6) &  1;
            p.eg = (data >> 5) &  1;
            p.kr = (data >> 4) &  1;
            p.ml = (data >> 0) & 15;
        }
        reg[rg] = data;
        break;
    }
    case 0x40: {
        if (int s = sTbl[rg & 0x1f]; s >= 0) {
            Patch &p = patch[s];
            p.kl = (data >> 6) &  3;
            p.tl = (data >> 0) & 63;
        }
        reg[rg] = data;
        break;
    }
    case 0x60: {
        if (int s = sTbl[rg & 0x1f]; s >= 0) {
            Patch &p = patch[s];
            p.ar = (data >> 4) & 15;
            p.dr = (data >> 0) & 15;
        }
        reg[rg] = data;
        break;
    }
    case 0x80: {
        if (int s = sTbl[rg & 0x1f]; s >= 0) {
            Patch &p = patch[s];
            p.sl = (data >> 4) & 15;
            p.rr = (data >> 0) & 15;
        }
        reg[rg] = data;
        break;
    }
    case 0xa0: {
        if (rg == 0xbd) {
            am_mode = (data & 0x80) != 0;
            pm_mode = (data & 0x40) != 0;

            reg_flags = data;
            reg[rg] = data;
            break;
        }
        unsigned c = rg & 0x0f;
        if (c > 8) {
            // 0xa9-0xaf 0xb9-0xbf
            break;
        }
        unsigned freq = [&] {
            if (!(rg & 0x10)) {
                // 0xa0-0xa8
                return data | ((reg[rg + 0x10] & 0x1F) << 8);
            } else {
                // 0xb0-0xb8
                channel[c].reg_key = (data >> 5) & 1;
                return reg[rg - 0x10] | ((data & 0x1F) << 8);
            }
        }();
        channel[c].reg_freq = freq;
        reg[rg] = data;
        break;
    }
    case 0xc0: {
        if (rg > 0xc8)
            break;
        int c = rg - 0xC0;
        Patch &p = patch[c*2+MOD];
        p.fb = (data >> 1) & 7;
        p.alg = data & 1; // TODO: Add support for 'amplitude modulation' algorithm
        assert((data & 1)==0); // only support alg==0
        reg[rg] = data;
    }
    }
}

uint8_t Y8950Tim::readReg(uint8_t rg, EmuTime::param time)
{
    updateStream(time); // TODO only when necessary

    switch (rg) {
        case 0x0F: // ADPCM-DATA
        case 0x13: //  ???
        case 0x14: //  ???
        case 0x1A: // PCM-DATA
            return adpcm.readReg(rg, time);
        default:
            return peekReg(rg, time);
    }
}

uint8_t Y8950Tim::peekReg(uint8_t rg, EmuTime::param time) const
{
    switch (rg) {
        case 0x05: // (KEYBOARD IN)
            return connector.peek(time);

        case 0x0F: // ADPCM-DATA
        case 0x13: //  ???
        case 0x14: //  ???
        case 0x1A: // PCM-DATA
            return adpcm.peekReg(rg, time);

        case 0x19: { // I/O DATA
            uint8_t input = periphery.read(time);
            uint8_t output = reg[0x19];
            uint8_t enable = reg[0x18];
            return (output & enable) | (input & ~enable) | 0xF0;
        }
        default:
            return reg[rg];
    }
}

uint8_t Y8950Tim::readStatus(EmuTime::param time) const
{
    return peekStatus(time);
}

uint8_t Y8950Tim::peekStatus(EmuTime::param time) const
{
    const_cast<Y8950TimAdpcm&>(adpcm).sync(time);
    return (status & (0x87 | statusMask)) | 0x06; // bit 1 and 2 are always 1
}

void Y8950Tim::callback(uint8_t flag)
{
    setStatus(flag);
}

void Y8950Tim::setStatus(uint8_t flags)
{
    status |= flags;
    if (status & statusMask) {
        status |= 0x80;
        irq.set();
    }
}
void Y8950Tim::resetStatus(uint8_t flags)
{
    status &= ~flags;
    if (!(status & statusMask)) {
        status &= 0x7f;
        irq.reset();
    }
}
uint8_t Y8950Tim::peekRawStatus() const
{
    return status;
}
void Y8950Tim::changeStatusMask(uint8_t newMask)
{
    statusMask = newMask;
    status &= 0x87 | statusMask;
    if (status & statusMask) {
        status |= 0x80;
        irq.set();
    } else {
        status &= 0x7f;
        irq.reset();
    }
}


template<typename Archive>
void Y8950Tim::Patch::serialize(Archive& ar, unsigned /*version*/)
{
    ar.serialize("AM", am,
                 "PM", pm,
                 "EG", eg,
                 "KR", kr,
                 "ML", ml,
                 "KL", kl,
                 "TL", tl,
                 "FB", fb,
                 "AR", ar,
                 "DR", dr,
                 "SL", sl,
                 "RR", rr);
}

template<typename Archive>
void Y8950Tim::serialize(Archive& ar, unsigned /*version*/)
{
    ar.serialize("keyboardConnector", connector,
                 "adpcm",             adpcm,
                 "timer1",            *timer1,
                 "timer2",            *timer2,
                 "irq",               irq);
    ar.serialize_blob("registers", reg);
    ar.serialize(
                 "status",        status,
                 "statusMask",    statusMask,
                 "rythm_mode",    rythm_mode,
                 "am_mode",       am_mode,
                 "pm_mode",       pm_mode,
                 "enabled",       enabled);
    if constexpr (Archive::IS_LOADER) {
        // TODO restore more state from registers
        static constexpr std::array<uint8_t, 2> rewriteRegs = {
            6,       // connector
            15,      // dac13
        };

        EmuTime::param time = motherBoard.getCurrentTime();
        for (auto r : rewriteRegs) {
            writeReg(r, reg[r], time);
        }
    }
}


// SimpleDebuggable

Y8950Tim::Debuggable::Debuggable(MSXMotherBoard& motherBoard_,
                              const std::string& name_)
    : SimpleDebuggable(motherBoard_, name_ + " regs", "MSX-AUDIO", 0x100)
{
}

uint8_t Y8950Tim::Debuggable::read(unsigned address, EmuTime::param time)
{
    auto& y8950 = OUTER(Y8950Tim, debuggable);
    return y8950.peekReg(narrow<uint8_t>(address), time);
}

void Y8950Tim::Debuggable::write(unsigned address, uint8_t value, EmuTime::param time)
{
    auto& y8950 = OUTER(Y8950Tim, debuggable);
    y8950.writeReg(narrow<uint8_t>(address), value, time);
}

INSTANTIATE_SERIALIZE_METHODS(Y8950Tim);

} // namespace openmsx
