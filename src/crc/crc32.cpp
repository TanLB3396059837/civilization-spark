#include "civ/crc/crc32.hpp"
#include <cstring>

#ifdef CIV_USE_HW_CRC32
#include <nmmintrin.h>
#endif

namespace civ {

    namespace {
        constexpr std::uint32_t POLY = 0x82F63B78u;

        struct Crc32Table {
            std::uint32_t t[256];
            constexpr Crc32Table() : t{} {
                for (std::uint32_t i = 0; i < 256; ++i) {
                    std::uint32_t c = i;
                    for (int j = 0; j < 8; ++j)
                        c = (c >> 1) ^ (POLY & static_cast<std::uint32_t>(-(int)(c & 1)));
                    t[i] = c;
                }
            }
        };
        constexpr Crc32Table kTable{};
    } // namespace

#ifdef CIV_USE_HW_CRC32
    static std::uint32_t hw_crc(std::uint32_t c, const Byte* p, std::size_t n) {
        while (n >= 8) {
            std::uint64_t word = 0;
            std::memcpy(&word, p, sizeof(word));
            c = static_cast<std::uint32_t>(_mm_crc32_u64(c, word));
            p += 8; n -= 8;
        }
        while (n) { c = _mm_crc32_u8(c, *p); ++p; --n; }
        return c;
    }
#endif

    std::uint32_t crc32(ByteView data) noexcept {
        std::uint32_t c = 0xFFFFFFFFu;
#ifdef CIV_USE_HW_CRC32
        c = hw_crc(c, data.data(), data.size());
#else
        for (auto b : data) c = kTable.t[(c ^ b) & 0xFF] ^ (c >> 8);
#endif
        return c ^ 0xFFFFFFFFu;
    }

} // namespace civ
