#pragma once
#include "../core/types.hpp"

namespace civ {
    std::uint32_t crc32(ByteView data) noexcept;
} // namespace civ