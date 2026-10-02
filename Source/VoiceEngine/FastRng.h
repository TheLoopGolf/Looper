#pragma once

#include <cstdint>

namespace looper {

/**
 * Tiny real-time-safe PRNG (PCG32, XSH-RR output; O'Neill 2014).
 * 16 bytes of state, no allocation, no locks, no syscalls: safe on the audio thread.
 * Seedable for deterministic tests. Not for cryptography.
 */
class FastRng
{
public:
    explicit FastRng(uint64_t seed = 0x853c49e6748fea9bULL) noexcept { seedWith(seed); }

    void seedWith(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) noexcept
    {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    /** Uniform 32-bit value. */
    uint32_t next() noexcept
    {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /**
     * Uniform integer in [0, n) (n > 0) via Lemire's multiply-shift.
     * Bias is < n / 2^32, i.e. negligible for round-robin sized n.
     */
    uint32_t nextBelow(uint32_t n) noexcept
    {
        return static_cast<uint32_t>((static_cast<uint64_t>(next()) * n) >> 32u);
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

} // namespace looper
