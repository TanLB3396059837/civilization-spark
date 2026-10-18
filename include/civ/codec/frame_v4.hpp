#pragma once
#include "../core/types.hpp"

namespace civ {

    // ============ KFL-M v4 帧格式 ============
    // [会话 4bit | 类型 4bit][索引 0-2B][CRC-8 1B]
    //
    // 类型:
    //   0x0        : 命中 (残差为零)
    //   0x1 - 0x7  : 内联索引 1-7
    //   0x8        : 8 bit 索引
    //   0x9        : 16 bit 索引

    struct DecodedFrame {
        Byte          session = 0;
        std::uint16_t index   = 0;
        bool          is_hit  = false;
        bool          crc_ok  = false;
    };

    std::size_t encode_index(std::uint16_t idx, Byte session,
                             Byte* out, std::size_t cap) noexcept;

    std::size_t encode_hit(Byte session, Byte* out, std::size_t cap) noexcept;

    DecodedFrame decode_frame(ByteView bytes) noexcept;

} // namespace civ