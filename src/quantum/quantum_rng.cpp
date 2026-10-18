#include "civ/quantum/quantum_rng.hpp"
#include <chrono>

namespace civ {

QuantumRNG::QuantumRNG(std::uint64_t seed) {
    std::uint64_t s = seed;
    if (s == 0) {
        s = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    state_[0] = s;
    state_[1] = s ^ 0x9E3779B97F4A7C15ULL;
    state_[2] = s ^ 0xBF58476D1CE4E5B9ULL;
    state_[3] = s ^ 0x94D049BB133111EBULL;
    for (int i = 0; i < 16; ++i) next_u64();
}

std::uint64_t QuantumRNG::next_u64() {
    auto rotl = [](std::uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    };
    std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
    std::uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
}

float QuantumRNG::next_float() {
    return static_cast<float>(next_u64() >> 40) / static_cast<float>(1 << 24);
}

// Lemire 无偏取模
std::uint32_t QuantumRNG::next_range(std::uint32_t lo, std::uint32_t hi) {
    if (hi <= lo) return lo;
    std::uint32_t range = hi - lo;
    // Lemire 算法要求“随机数 × 范围”的乘积不超过 64 bit。
    // 这里先取 32 bit 随机数，再与 32 bit 范围相乘，
    // 避免 64 bit 随机数直接相乘后溢出、取到错误的中间位。
    std::uint32_t r = static_cast<std::uint32_t>(next_u64() >> 32);
    std::uint64_t m = static_cast<std::uint64_t>(r) * range;
    std::uint32_t result = static_cast<std::uint32_t>(m >> 32);
    if (static_cast<std::uint32_t>(m) < range) {
        std::uint32_t threshold = (0u - range) % range;
        while (static_cast<std::uint32_t>(m) < threshold) {
            r = static_cast<std::uint32_t>(next_u64() >> 32);
            m = static_cast<std::uint64_t>(r) * range;
            result = static_cast<std::uint32_t>(m >> 32);
        }
    }
    return lo + result;
}

std::array<Byte, 32> QuantumRNG::generate_qkd_key() {
    std::array<Byte, 32> key{};
    for (std::size_t i = 0; i < 32; i += 8) {
        std::uint64_t r = next_u64();
        for (int j = 0; j < 8; ++j) {
            key[i + j] = static_cast<Byte>(r & 0xFF);
            r >>= 8;
        }
    }
    return key;
}

// 熵分散到 4 个 state
void QuantumRNG::mix_entropy(std::uint64_t entropy) {
    auto rotl = [](std::uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    };
    state_[0] ^= entropy;
    state_[1] ^= rotl(entropy, 17);
    state_[2] ^= rotl(entropy, 33);
    state_[3] ^= rotl(entropy, 49);
    for (int i = 0; i < 8; ++i) next_u64();
}

std::uint64_t QuantumRNG::state_hash() const {
    std::uint64_t h = 0;
    for (auto s : state_) h ^= s + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

void QuantumRNG::jump(std::uint64_t n) {
    for (std::uint64_t i = 0; i < n; ++i) next_u64();
}

} // namespace civ
