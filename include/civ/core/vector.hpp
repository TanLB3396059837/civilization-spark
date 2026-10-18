#pragma once
#include "config.hpp"
#include <array>
#include <cmath>

namespace civ {

    struct alignas(64) Vector {
        std::array<float, DIM> v;

        Vector() { v.fill(0.0f); }

        float& operator[](std::size_t i)       { return v[i]; }
        float  operator[](std::size_t i) const { return v[i]; }

        void sub(const Vector& o, Vector& out) const {
            for (std::size_t i = 0; i < DIM; ++i) out.v[i] = v[i] - o.v[i];
        }

        float energy() const {
            float e = 0.0f;
            for (auto x : v) e += x * x;
            return e;
        }

        float l1_error(const Vector& o) const {
            float s = 0.0f;
            for (std::size_t i = 0; i < DIM; ++i) s += std::abs(v[i] - o.v[i]);
            return s / DIM;
        }
    };

} // namespace civ