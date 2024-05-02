#pragma once

#include "YM2413Core.hh"
#include "YM2413TimChannel.hh"
#include "FixedPoint.hh"
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

        [[nodiscard]] Patch& getPatch(unsigned instrument, bool carrier);

        template<typename Archive>
        void serialize(Archive& ar, unsigned version);

    private:
        void writeReg(uint8_t r, uint8_t data);

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
        [[nodiscard]] bool isRhythm() const;
        [[nodiscard]] unsigned getFreq(unsigned channel) const;

        template<unsigned FLAGS>
        void calcChannel(Channel& ch, std::span<float> buf);

    private:
        /** Channel & Slot */
        std::array<Channel, 9> channels;

        /** Pitch Modulator */
        unsigned pm_phase;

        /** Amp Modulator */
        unsigned am_phase;

        /** Noise Generator */
        unsigned noise_seed;

        /** Voice Data */
        std::array<std::array<Patch, 2>, 19> patches;

        /** Registers */
        std::array<uint8_t, 0x40> reg;
        uint8_t registerLatch;
    };

} // namespace YM2413Tim

SERIALIZE_CLASS_VERSION(YM2413Tim::Slot, 4);
SERIALIZE_CLASS_VERSION(YM2413Tim::Channel, 2);
SERIALIZE_CLASS_VERSION(YM2413Tim::YM2413, 4);

} // namespace openmsx
