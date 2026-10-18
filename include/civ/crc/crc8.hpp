#pragma once
#include "../core/types.hpp"

namespace civ {
    Byte crc8(ByteView data) noexcept;
} // namespace civ