#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <string>
#include <array>

// ============================================================
// ★ v3.3：用 #ifndef 保护，避免与 CMake add_compile_definitions
//   重复定义产生的 C4005 warning
// ============================================================
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace civ {

    using Byte     = std::uint8_t;
    using Bytes    = std::vector<Byte>;
    using ByteView = std::span<const Byte>;

    using AgentId  = std::uint32_t;
    using FileId   = std::uint32_t;
    using BlockSeq = std::uint64_t;

} // namespace civ