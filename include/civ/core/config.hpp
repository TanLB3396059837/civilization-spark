#pragma once
#include <cstddef>
#include <cstdint>

namespace civ {

    inline constexpr std::size_t DIM           = 128;       // 语义向量维度
    inline constexpr std::size_t CODEBOOK_SIZE = 1 << 16;   // 码本容量
    inline constexpr std::size_t MAX_AGENTS    = 13;        // 智能体数
    inline constexpr std::size_t MAX_ROUNDS    = 13;        // 一个轮回的泰坦关卡数

    inline constexpr std::size_t BLOCK_SIZE    = 64 * 1024;
    inline constexpr std::size_t RDMA_BUF_SIZE = 1 << 20;

} // namespace civ
