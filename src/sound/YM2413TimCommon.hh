#pragma once

#include "YM2413Core.hh"
#include "serialize_meta.hh"
#include <array>
#include <span>

namespace openmsx {
namespace YM2413Tim {

    class YM2413;

    // Bits for linear value
    static constexpr int DB2LIN_AMP_BITS = 8;

} // namespace YM2413Tim
} // namespace openmsx
