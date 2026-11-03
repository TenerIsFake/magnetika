#pragma once
// Math helpers that reproduce JavaScript (V8) results where the C++ library differs.

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace magnetika::js {

// V8's Math.hypot: scale by the largest magnitude, Kahan-sum the squares, sqrt,
// rescale. std::hypot uses a different algorithm and differs in the last bit,
// which a feedback loop then amplifies. (V8 src/builtins/math.tq, MathHypot.)
inline double hypot(double a, double b) {
    if (std::isinf(a) || std::isinf(b)) return INFINITY;
    if (std::isnan(a) || std::isnan(b)) return NAN;
    const double x = std::fabs(a), y = std::fabs(b);
    double max = 0.0;
    if (x > max) max = x;
    if (y > max) max = y;
    if (max == 0.0) return 0.0;
    double sum = 0.0, compensation = 0.0;
    for (double v : {x, y}) {
        const double n = v / max;
        const double summand = n * n - compensation;
        const double preliminary = sum + summand;
        compensation = (preliminary - sum) - summand;
        sum = preliminary;
    }
    return std::sqrt(sum) * max;
}

// Math.min(1, x) for the non-NaN inputs the engine produces.
inline double min1(double x) { return x < 1.0 ? x : 1.0; }

// xorshift32, exactly as the seeded reference engine (int32/uint32 JS bit ops).
struct XorShift32 {
    std::uint32_t state = 0x9E3779B9u;
    void seed(std::uint32_t s) { state = s ? s : 0x9E3779B9u; }
    double next() {  // [-1, 1)
        std::uint32_t s = state;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        state = s;
        return static_cast<double>(state) / 4294967296.0 * 2.0 - 1.0;
    }
};

} // namespace magnetika::js
