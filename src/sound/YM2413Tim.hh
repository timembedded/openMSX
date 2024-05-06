#pragma once

#include "YM2413Core.hh"
#include "YM2413TimChannel.hh"
#include "serialize_meta.hh"
#include <array>
#include <span>

namespace openmsx {
namespace YM2413Tim {

class YM2413 final : public YM2413Core
{
public:
    YM2413();

    // YM2413Core
    void reset() override;
    void writePort(bool port, uint8_t value, int offset) override;
    void pokeReg(uint8_t reg, uint8_t data) override;
    [[nodiscard]] uint8_t peekReg(uint8_t reg) const override;
    void generateChannels(std::span<float*, 9 + 5> bufs, unsigned num) override;
    [[nodiscard]] float getAmplificationFactor() const override;

    template<typename Archive>
    void serialize(Archive& ar, unsigned version);

private:
    void writeReg(uint8_t r, uint8_t data);
    void writePatchReg(uint8_t r, uint8_t data);

    void keyOn_BD();
    void keyOn_SD();
    void keyOn_TOM();
    void keyOn_HH();
    void keyOn_CYM();
    void keyOff_BD();
    void keyOff_SD();
    void keyOff_TOM();
    void keyOff_HH();
    void keyOff_CYM();
    void setRhythmFlags(uint8_t old);
    void update_key_status();
    bool isRhythm() const;
    uint16_t getFreq(unsigned channel) const;

    void calcChannel(Channel& ch, uint8_t FLAGS, std::span<float> buf);

private:
    /** Channel & Slot */
    Slot &slot;
    std::array<Channel, 9> channels;

    /** Pitch Modulator */
    unsigned pm_phase;

    /** Amp Modulator */
    unsigned am_phase;

    /** Noise Generator */
    unsigned noise_seed;

    /** Voice Data */
    Patch &patch;

    /** Registers */
    uint8_t reg_flags;
    std::array<uint8_t, 8> reg_instr;
    std::array<uint16_t, 9> reg_freq; // 12-bit
    std::array<uint8_t, 9> reg_volume; // 0-15
    std::array<uint8_t, 9> reg_patch;  // 0-15
    std::array<uint8_t, 9> reg_key;  // 1-bit
    uint8_t registerLatch;

    /** Patches */
    int getPatch(unsigned instrument, bool carrier);
};

} // namespace YM2413Tim

SERIALIZE_CLASS_VERSION(YM2413Tim::Slot, 4);
SERIALIZE_CLASS_VERSION(YM2413Tim::Channel, 2);
SERIALIZE_CLASS_VERSION(YM2413Tim::YM2413, 4);

} // namespace openmsx
