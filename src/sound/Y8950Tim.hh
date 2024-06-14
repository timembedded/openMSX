#pragma once

#include "Y8950TimAdpcm.hh"
#include "Y8950KeyboardConnector.hh"
#include "ResampledSoundDevice.hh"
#include "DACSound16S.hh"

#include "SimpleDebuggable.hh"
#include "IRQHelper.hh"
#include "EmuTimer.hh"
#include "EmuTime.hh"
#include "FixedPoint.hh"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <memory>

#include "YM2413TimSlot.hh"

namespace openmsx {

class MSXAudio;
class DeviceConfig;
class Y8950Periphery;

class Y8950Tim final : private ResampledSoundDevice, private EmuTimerCallback
{
public:
    static constexpr int CLOCK_FREQ     = 3579545;
    static constexpr int CLOCK_FREQ_DIV = 72;

    // Bitmask for register 0x04
    // Timer1 Start.
    static constexpr int R04_ST1          = 0x01;
    // Timer2 Start.
    static constexpr int R04_ST2          = 0x02;
    // not used
    //static constexpr int R04            = 0x04;
    // Mask 'Buffer Ready'.
    static constexpr int R04_MASK_BUF_RDY = 0x08;
    // Mask 'End of sequence'.
    static constexpr int R04_MASK_EOS     = 0x10;
    // Mask Timer2 flag.
    static constexpr int R04_MASK_T2      = 0x20;
    // Mask Timer1 flag.
    static constexpr int R04_MASK_T1      = 0x40;
    // IRQ RESET.
    static constexpr int R04_IRQ_RESET    = 0x80;

    // Bitmask for status register
    static constexpr int STATUS_PCM_BSY = 0x01;
    static constexpr int STATUS_EOS     = R04_MASK_EOS;
    static constexpr int STATUS_BUF_RDY = R04_MASK_BUF_RDY;
    static constexpr int STATUS_T2      = R04_MASK_T2;
    static constexpr int STATUS_T1      = R04_MASK_T1;

    Y8950Tim(const std::string& name, const DeviceConfig& config,
          unsigned sampleRam, EmuTime::param time, MSXAudio& audio);
    ~Y8950Tim();

    void setEnabled(bool enabled, EmuTime::param time);
    void clearRam();
    void reset(EmuTime::param time);
    void writeReg(uint8_t rg, uint8_t data, EmuTime::param time);
    [[nodiscard]] uint8_t readReg(uint8_t rg, EmuTime::param time);
    [[nodiscard]] uint8_t peekReg(uint8_t rg, EmuTime::param time) const;
    [[nodiscard]] uint8_t readStatus(EmuTime::param time) const;
    [[nodiscard]] uint8_t peekStatus(EmuTime::param time) const;

    // for ADPCM
    void setStatus(uint8_t flags);
    void resetStatus(uint8_t flags);
    [[nodiscard]] uint8_t peekRawStatus() const;

    template<typename Archive>
    void serialize(Archive& ar, unsigned version);

private:
    // SoundDevice
    [[nodiscard]] float getAmplificationFactorImpl() const override;
    void generateChannels(std::span<float*> bufs, unsigned num) override;

    void changeStatusMask(uint8_t newMask);

    void callback(uint8_t flag) override;

    class Patch {
    public:
        Patch();
        void reset();

        template<typename Archive>
        void serialize(Archive& ar, unsigned version);

        bool am = false;
        bool pm = false;
        bool eg = false;
        uint8_t kr = 0; // 0,1
        uint8_t ml = 0; // 0-15
        uint8_t kl = 0; // 0-3
        uint8_t tl = 0; // 0-63
        uint8_t fb = 0; // 0,1-7
        uint8_t ar = 0; // 0-15
        uint8_t dr = 0; // 0-15
        uint8_t sl = 0; // 0-15
        uint8_t rr = 0; // 0-15
        uint8_t alg = 0; // 0,1 (0=FM, 1=AM)
    };

    class Channel {
    public:
        Channel();
        void reset();

        template<typename Archive>
        void serialize(Archive& ar, unsigned version);

        uint8_t  reg_key;  // 1-bit
        uint16_t reg_freq;
    };

    MSXMotherBoard& motherBoard;
    Y8950Periphery& periphery;
    Y8950TimAdpcm adpcm;
    Y8950KeyboardConnector connector;
    DACSound16S dac13; // 13-bit (exponential) DAC

    struct Debuggable final : SimpleDebuggable {
        Debuggable(MSXMotherBoard& motherBoard, const std::string& name);
        [[nodiscard]] uint8_t read(unsigned address, EmuTime::param time) override;
        void write(unsigned address, uint8_t value, EmuTime::param time) override;
    } debuggable;

    const std::unique_ptr<EmuTimer> timer1; //  80us timer
    const std::unique_ptr<EmuTimer> timer2; // 320us timer
    IRQHelper irq;

    std::array<Patch, 18> patch;

    std::array<Channel, 9> channel;

    std::array<uint8_t, 0x100> reg;
    uint8_t reg_flags = 0;

    uint8_t status;     // STATUS Register
    uint8_t statusMask; // bit=0 -> masked
    bool rythm_mode;
    bool am_mode;
    bool pm_mode;
    bool enabled = true;

    YM2413Tim::Slot slot;
};

} // namespace openmsx
