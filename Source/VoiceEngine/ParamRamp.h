#pragma once

#include <cmath>
#include <functional>

namespace looper {

/**
 * Linear per-sample ramp toward a target (parameter smoothing without zipper noise).
 * The last step lands exactly on the target, so a settled ramp returns the target value
 * bit-for-bit (static settings render exactly as if they were never smoothed).
 * Real-time safe: no allocation, no locks.
 */
struct ParamRamp
{
    /** Exact float comparison (std::equal_to keeps -Wfloat-equal quiet: exactness is intended). */
    static bool same(float a, float b) noexcept { return std::equal_to<float>()(a, b); }

    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int remaining = 0;

    /** Jump straight to v (no ramp). */
    void snap(float v) noexcept
    {
        current = target = v;
        step = 0.0f;
        remaining = 0;
    }

    /** Ramp from the current value to v over `samples` (<= 0 snaps). No-op if v is already the target. */
    void setTarget(float v, int samples) noexcept
    {
        if (same(v, target))
            return;
        if (samples <= 0)
        {
            snap(v);
            return;
        }
        target = v;
        step = (target - current) / static_cast<float>(samples);
        remaining = samples;
    }

    /** Advance one sample and return the new value. */
    float next() noexcept
    {
        if (remaining > 0)
        {
            if (--remaining == 0)
                current = target;
            else
                current += step;
        }
        return current;
    }

    bool ramping() const noexcept { return remaining > 0; }
    /** Zero now and at the end of the ramp (contributes nothing). */
    bool isZero() const noexcept { return same(current, 0.0f) && same(target, 0.0f); }

    /** Smoothing time used for the engine's continuous parameters. */
    static constexpr double kSeconds = 0.02;
    static int samplesFor(double sampleRate) noexcept
    {
        return sampleRate > 0.0 ? static_cast<int>(std::lround(sampleRate * kSeconds)) : 0;
    }
};

} // namespace looper
