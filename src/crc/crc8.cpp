#include "civ/crc/crc8.hpp"

namespace civ {

    namespace {
        constexpr Byte POLY = 0x07;

        struct Crc8Table {
            Byte t[256];
            constexpr Crc8Table() : t{} {
                for (int i = 0; i < 256; ++i) {
                    Byte c = static_cast<Byte>(i);
                    for (int j = 0; j < 8; ++j)
                        c = (c & 0x80) ? static_cast<Byte>((c << 1) ^ POLY)
                                       : static_cast<Byte>(c << 1);
                    t[i] = c;
                }
            }
        };
        constexpr Crc8Table kTable{};
    } // namespace

    Byte crc8(ByteView data) noexcept {
        Byte c = 0;
        for (auto b : data) c = kTable.t[c ^ b];
        return c;
    }

} // namespace civ