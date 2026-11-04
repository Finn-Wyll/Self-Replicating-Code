#pragma once
// Deterministic, platform-independent random numbers.
// std::*_distribution differs between standard libraries (libstdc++ vs libc++),
// so all distributions are implemented here to keep runs reproducible on Linux and macOS.
#include <cmath>
#include <cstdint>

namespace adaptogen {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        std::uint64_t x = seed;
        for (auto& s : s_) s = splitmix(x);
    }

    // xoshiro256**
    std::uint64_t next() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    /// Uniform double in [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

    /// Unbiased integer in [0, n). Returns 0 when n == 0.
    std::uint64_t below(std::uint64_t n) {
        if (n == 0) return 0;
        const std::uint64_t threshold = (0 - n) % n;
        for (;;) {
            const std::uint64_t r = next();
            if (r >= threshold) return r % n;
        }
    }

    /// Integer in [lo, hi] (inclusive).
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(below(static_cast<std::uint64_t>(hi - lo) + 1));
    }

    bool chance(double p) { return uniform() < p; }

    /// Standard normal (Box-Muller, no caching so the stream stays simple).
    double normal() {
        double u1 = 1.0 - uniform();  // (0, 1]
        double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    /// Independent child generator.
    Rng split() { return Rng(next()); }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static std::uint64_t splitmix(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    std::uint64_t s_[4]{};
};

}  // namespace adaptogen
