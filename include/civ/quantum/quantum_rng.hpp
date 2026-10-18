#pragma once
#include "../core/types.hpp"
#include <cstdint>
#include <array>
#include <limits>

namespace civ {

    class QuantumRNG {
    public:
        explicit QuantumRNG(std::uint64_t seed = 0);
        std::uint64_t next_u64();
        float next_float();
        std::uint32_t next_range(std::uint32_t lo, std::uint32_t hi);
        std::array<Byte, 32> generate_qkd_key();
        void mix_entropy(std::uint64_t entropy);
        std::uint64_t state_hash() const;
        void jump(std::uint64_t n);

    private:
        std::array<std::uint64_t, 4> state_;
    };

    // URBG 包装（让 QuantumRNG 可用于 std::discrete_distribution 等）
    class QuantumURBG {
    public:
        using result_type = std::uint64_t;
        explicit QuantumURBG(QuantumRNG& rng) noexcept : rng_(&rng) {}
        static constexpr result_type min() noexcept { return 0; }
        static constexpr result_type max() noexcept {
            return std::numeric_limits<result_type>::max();
        }
        result_type operator()() noexcept { return rng_->next_u64(); }

    private:
        QuantumRNG* rng_;
    };

} // namespace civ