#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace looper {

/**
 * 4-point cubic Hermite (Catmull–Rom style) interpolation.
 * Interpolates between y1 and y2; y0/y3 are neighbors. t in [0,1].
 * Linear curves are OK for the amp envelope milestone; Hermite is the
 * sample-read quality tier (design default).
 */
inline float hermite4(float y0, float y1, float y2, float y3, float t) noexcept
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

/** Safe sample fetch: out-of-range indices return 0 (zero-pad). */
inline float sampleAt(const float* data, int64_t index, int64_t length) noexcept
{
    if (index < 0 || index >= length)
        return 0.0f;
    return data[index];
}

/**
 * Read one mono frame at fractional position `pos` from a mono buffer
 * (one float per frame). Edges zero-padded.
 */
inline float hermiteMono(const float* data, int64_t numFrames, double pos) noexcept
{
    const int64_t i1 = static_cast<int64_t>(std::floor(pos));
    const float t = static_cast<float>(pos - static_cast<double>(i1));
    const float y0 = sampleAt(data, i1 - 1, numFrames);
    const float y1 = sampleAt(data, i1, numFrames);
    const float y2 = sampleAt(data, i1 + 1, numFrames);
    const float y3 = sampleAt(data, i1 + 2, numFrames);
    return hermite4(y0, y1, y2, y3, t);
}

/**
 * Read one stereo frame (L,R) at fractional frame position from
 * interleaved stereo (LRLR...). Edges zero-padded per channel.
 */
inline void hermiteStereo(const float* interleaved, int64_t numFrames, double pos,
                          float& outL, float& outR) noexcept
{
    const int64_t i1 = static_cast<int64_t>(std::floor(pos));
    const float t = static_cast<float>(pos - static_cast<double>(i1));
    const int64_t n = numFrames;

    auto ch = [&](int64_t frame, int channel) -> float {
        if (frame < 0 || frame >= n)
            return 0.0f;
        return interleaved[frame * 2 + channel];
    };

    outL = hermite4(ch(i1 - 1, 0), ch(i1, 0), ch(i1 + 1, 0), ch(i1 + 2, 0), t);
    outR = hermite4(ch(i1 - 1, 1), ch(i1, 1), ch(i1 + 1, 1), ch(i1 + 2, 1), t);
}

/**
 * Read from interleaved buffer with 1 or 2 channels.
 * Mono is duplicated to L/R.
 */
inline void hermiteRead(const float* interleaved, int channels, int64_t numFrames,
                        double pos, float& outL, float& outR) noexcept
{
    if (channels >= 2)
    {
        hermiteStereo(interleaved, numFrames, pos, outL, outR);
    }
    else
    {
        const float m = hermiteMono(interleaved, numFrames, pos);
        outL = m;
        outR = m;
    }
}

} // namespace looper
