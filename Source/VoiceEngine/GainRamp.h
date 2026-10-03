#pragma once

namespace looper {

/**
 * Linear per-sample ramp for a voice's zone gain (JUCE-free, real-time safe: no allocation,
 * no locks). Same idea as juce::SmoothedValue<float, Linear>: a new target is reached over a
 * fixed number of samples so live gain edits on sounding notes never click.
 */
struct GainRamp
{
    /** Ramp length used for live zone-gain edits. */
    static constexpr double kRampSeconds = 0.020;

    float current = 1.0f;
    float target = 1.0f;
    float step = 0.0f;
    int remaining = 0;

    /** Jump straight to `value` (note-on: a new voice starts at its zone gain). */
    void reset (float value) noexcept
    {
        current = target = value;
        step = 0.0f;
        remaining = 0;
    }

    /** Glide from the current value to `value` over `samples` samples (<= 0: jump). */
    void setTarget (float value, int samples) noexcept
    {
        target = value;
        if (samples <= 0)
        {
            reset (value);
            return;
        }
        remaining = samples;
        step = (target - current) / static_cast<float> (samples);
    }

    /** Advance one sample and return the gain for it. */
    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    bool isRamping() const noexcept { return remaining > 0; }

    static int samplesFor (double sampleRate) noexcept
    {
        const double n = kRampSeconds * (sampleRate > 0.0 ? sampleRate : 44100.0);
        return n < 1.0 ? 1 : static_cast<int> (n + 0.5);
    }
};

} // namespace looper
