#pragma once

#include "YM2413Core.hh"
#include "FixedPoint.hh"
#include "serialize_meta.hh"
#include <array>
#include <span>

namespace openmsx {
namespace YM2413Tim {

    class YM2413;

    inline constexpr int EP_FP_BITS = 15;
    using EnvPhaseIndex = FixedPoint<EP_FP_BITS>;

    // Size of sin table
    inline constexpr int PG_BITS = 9;
    inline constexpr int PG_WIDTH = 1 << PG_BITS;
    inline constexpr int PG_MASK = PG_WIDTH - 1;

    enum EnvelopeState {
        ATTACK, DECAY, SUSHOLD, SUSTAIN, RELEASE, SETTLE, FINISH
    };

    // Number of bits in 'PhaseModulation' fixed point type.
    static constexpr int PM_FP_BITS = 8;

    // Dynamic range (Accuracy of sin table)
    static constexpr int DB_BITS = 8;
    static constexpr int DB_MUTE = 1 << DB_BITS;
    static constexpr int DBTABLEN = 4 * DB_MUTE; // enough to not have to check for overflow

    static constexpr double DB_STEP = 48.0 / DB_MUTE;
    static constexpr double EG_STEP = 0.375;
    static constexpr double TL_STEP = 0.75;

    // Phase increment counter
    static constexpr int DP_BITS = 18;
    static constexpr int DP_BASE_BITS = DP_BITS - PG_BITS;

    // Dynamic range of envelope
    static constexpr int EG_BITS = 7;

    // Bits for linear value
    static constexpr int DB2LIN_AMP_BITS = 8;
    static constexpr int SLOT_AMP_BITS = DB2LIN_AMP_BITS;

    // Bits for Amp modulator
    static constexpr int AM_PG_BITS = 8;
    static constexpr int AM_PG_WIDTH = 1 << AM_PG_BITS;
    static constexpr int AM_DP_BITS = 16;
    static constexpr int AM_DP_WIDTH = 1 << AM_DP_BITS;
    static constexpr int AM_DP_MASK = AM_DP_WIDTH - 1;

    // LFO Amplitude Modulation table (verified on real YM3812)
    // 27 output levels (triangle waveform);
    // 1 level takes one of: 192, 256 or 448 samples
    //
    // Length: 210 elements.
    //  Each of the elements has to be repeated
    //  exactly 64 times (on 64 consecutive samples).
    //  The whole table takes: 64 * 210 = 13440 samples.
    //
    // Verified on real YM3812 (OPL2), but I believe it's the same for YM2413
    // because it closely matches the YM2413 AM parameters:
    //    speed = 3.7Hz
    //    depth = 4.875dB
    // Also this approach can be easily implemented in HW, the previous one (see SVN
    // history) could not.
    static constexpr unsigned LFO_AM_TAB_ELEMENTS = 210;

    // Extra (derived) constants
    static constexpr EnvPhaseIndex EG_DP_MAX = EnvPhaseIndex(1 << 7);
    static constexpr EnvPhaseIndex EG_DP_INF = EnvPhaseIndex(1 << 8); // as long as it's bigger

    //
    // Helper functions
    //
#if 0
    static constexpr unsigned EG2DB(unsigned d);
    static constexpr unsigned TL2EG(unsigned d);
    static constexpr unsigned DB_POS(double x);
    static constexpr unsigned DB_NEG(double x);
    static constexpr /*bool*/ int BIT(unsigned s, unsigned b);
#endif

    static constexpr unsigned EG2DB(unsigned d)
    {
        return d * narrow_cast<unsigned>(EG_STEP / DB_STEP);
    }
    static constexpr unsigned TL2EG(unsigned d)
    {
        assert(d < 64); // input is in range [0..63]
        return d * narrow_cast<unsigned>(TL_STEP / EG_STEP);
    }

    static constexpr unsigned DB_POS(double x)
    {
        auto result = int(x / DB_STEP);
        assert(0 <= result);
        assert(result < DB_MUTE);
        return result;
    }
    static constexpr unsigned DB_NEG(double x)
    {
        return DBTABLEN + DB_POS(x);
    }

    // Note: 'int' instead of 'bool' return value to silence clang-warning:
    //    warning: use of bitwise '&' with boolean operands [-Wbitwise-instead-of-logical]
    //    note: cast one or both operands to int to silence this warning
    static constexpr /*bool*/ int BIT(unsigned s, unsigned b)
    {
        return narrow<int>((s >> b) & 1);
    }

    #include "YM2413TimTables.ii"
/*
    // LFO Phase Modulation table (copied from Burczynski core)
    static constexpr std::array pmTable;
    static constexpr std::array<uint8_t, 16> mlTable;
    static constexpr std::array<int, 2 * DBTABLEN> dB2LinTab;
    static constexpr std::array<unsigned, 1 << EG_BITS> arAdjustTab;
    static constexpr std::array<std::array<uint8_t, 16 * 8>, 4> tllTab;
    static constexpr std::array<unsigned, PG_WIDTH> fullSinTable;
    static constexpr std::array<unsigned, PG_WIDTH> halfSinTable;
    static constexpr std::array<std::span<const unsigned, PG_WIDTH>, 2> waveform;
    static constexpr std::array<std::array<int, 16>, 16> dPhaseDrTab;
    static const unsigned slTab[16];
*/
} // namespace YM2413Tim
} // namespace openmsx
