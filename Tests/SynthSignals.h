#pragma once
// Synthetic test signals for pitch-detection tests (header-only, no JUCE).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace synth {

constexpr double kTwoPi = 6.283185307179586;

inline double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

inline std::vector<float> sine(double hz, double sr, double sec, double amp = 0.5, double dc = 0.0)
{
    std::vector<float> v(static_cast<size_t>(sr * sec));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(dc + amp * std::sin(kTwoPi * hz * static_cast<double>(i) / sr));
    return v;
}

/** Band-limited sawtooth (additive, harmonics below Nyquist). */
inline std::vector<float> saw(double hz, double sr, double sec, double amp = 0.4)
{
    std::vector<float> v(static_cast<size_t>(sr * sec));
    const int nh = static_cast<int>((sr * 0.45) / hz);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        double s = 0.0;
        for (int k = 1; k <= nh; ++k)
            s += std::sin(kTwoPi * hz * k * t) / k;
        v[i] = static_cast<float>(amp * s * 0.6);
    }
    return v;
}

/** Tone whose 2nd harmonic dominates the fundamental (octave-error bait). */
inline std::vector<float> strongSecondHarmonic(double hz, double sr, double sec)
{
    std::vector<float> v(static_cast<size_t>(sr * sec));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        v[i] = static_cast<float>(0.15 * std::sin(kTwoPi * hz * t) + 0.5 * std::sin(kTwoPi * 2 * hz * t)
                                  + 0.2 * std::sin(kTwoPi * 3 * hz * t));
    }
    return v;
}

/** Deterministic LCG white noise in [-1, 1]. */
struct Lcg
{
    uint32_t s = 12345u;
    float next()
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>((s >> 8) & 0xFFFFFF) / 8388607.5f - 1.0f;
    }
};

inline std::vector<float> noise(double sr, double sec, double amp = 0.5, uint32_t seed = 12345u)
{
    Lcg g;
    g.s = seed;
    std::vector<float> v(static_cast<size_t>(sr * sec));
    for (auto& x : v)
        x = static_cast<float>(amp) * g.next();
    return v;
}

/** Pluck-ish: 15 ms noise burst + harmonic tone with exponential decay. */
inline std::vector<float> pluck(double hz, double sr, double sec)
{
    std::vector<float> v(static_cast<size_t>(sr * sec));
    Lcg g;
    const size_t burst = static_cast<size_t>(0.015 * sr);
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double env = std::exp(-3.0 * t);
        double s = std::sin(kTwoPi * hz * t) + 0.5 * std::sin(kTwoPi * 2 * hz * t)
                 + 0.3 * std::sin(kTwoPi * 3 * hz * t) + 0.15 * std::sin(kTwoPi * 4 * hz * t);
        s *= 0.35 * env;
        if (i < burst)
            s += 0.8 * g.next() * (1.0 - static_cast<double>(i) / static_cast<double>(burst));
        v[i] = static_cast<float>(s);
    }
    return v;
}

/** Decaying noise hit (drum-like). */
inline std::vector<float> noiseHit(double sr, double sec)
{
    auto v = noise(sr, sec, 0.9, 777u);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] *= static_cast<float>(std::exp(-12.0 * static_cast<double>(i) / sr));
    return v;
}

/** Interleave two mono buffers. */
inline std::vector<float> interleave(const std::vector<float>& l, const std::vector<float>& r)
{
    const size_t n = std::min(l.size(), r.size());
    std::vector<float> out(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        out[2 * i] = l[i];
        out[2 * i + 1] = r[i];
    }
    return out;
}

} // namespace synth
