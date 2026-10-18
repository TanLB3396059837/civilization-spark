#include "civ/codec/frame_v4.hpp"
#include "civ/crc/crc8.hpp"

namespace civ {

// 按类型分别检查 cap
std::size_t encode_index(std::uint16_t idx, Byte session,
                         Byte* out, std::size_t cap) noexcept {
    if (!out) return 0;
    session &= 0x0F;   // 帧头只有 4 bit 会话号，避免污染类型位

    if (idx >= 1 && idx <= 7) {
        if (cap < 2) return 0;
        out[0] = static_cast<Byte>((session << 4) | idx);
        out[1] = crc8(ByteView(out, 1));
        return 2;
    }
    if (idx <= 255) {
        if (cap < 3) return 0;
        out[0] = static_cast<Byte>((session << 4) | 0x8);
        out[1] = static_cast<Byte>(idx);
        out[2] = crc8(ByteView(out, 2));
        return 3;
    }
    if (cap < 4) return 0;
    out[0] = static_cast<Byte>((session << 4) | 0x9);
    out[1] = static_cast<Byte>(idx >> 8);
    out[2] = static_cast<Byte>(idx & 0xFF);
    out[3] = crc8(ByteView(out, 3));
    return 4;
}

std::size_t encode_hit(Byte session, Byte* out, std::size_t cap) noexcept {
    if (!out) return 0;
    session &= 0x0F;
    if (cap < 2) return 0;
    out[0] = static_cast<Byte>(session << 4);
    out[1] = crc8(ByteView(out, 1));
    return 2;
}

// CRC 失败时清零
static void reset_frame(DecodedFrame& df) noexcept {
    df.session = 0;
    df.index = 0;
    df.is_hit = false;
    df.crc_ok = false;
}

DecodedFrame decode_frame(ByteView bytes) noexcept {
    DecodedFrame df{};
    if (bytes.empty()) return df;

    df.session = bytes[0] >> 4;
    Byte type = bytes[0] & 0x0F;

    if (type == 0) {
        df.is_hit = true;
        if (bytes.size() >= 2)
            df.crc_ok = (crc8(bytes.subspan(0, 1)) == bytes[1]);
        if (!df.crc_ok) reset_frame(df);
        return df;
    }
    if (type >= 1 && type <= 7) {
        df.index = type;
        if (bytes.size() >= 2)
            df.crc_ok = (crc8(bytes.subspan(0, 1)) == bytes[1]);
        if (!df.crc_ok) reset_frame(df);
        return df;
    }
    if (type == 0x8 && bytes.size() >= 3) {
        df.index = bytes[1];
        df.crc_ok = (crc8(bytes.subspan(0, 2)) == bytes[2]);
        if (!df.crc_ok) reset_frame(df);
        return df;
    }
    if (type == 0x9 && bytes.size() >= 4) {
        df.index = static_cast<std::uint16_t>((bytes[1] << 8) | bytes[2]);
        df.crc_ok = (crc8(bytes.subspan(0, 3)) == bytes[3]);
        if (!df.crc_ok) reset_frame(df);
        return df;
    }
    reset_frame(df);
    return df;
}

} // namespace civ
