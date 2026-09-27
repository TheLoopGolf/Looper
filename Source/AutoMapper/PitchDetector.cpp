#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace looper {
namespace {

constexpr double kPi = 3.14159265358979323846;

using cd = std::complex<double>;

/** In-place iterative radix-2 FFT (size must be a power of two). */
void fft(std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const cd wlen(std::cos(ang), std::sin(ang));
        const size_t half = len / 2;
        // Precompute twiddles for this stage (more accurate than recurrence over long runs)
        std::vector<cd> tw(half);
        tw[0] = cd(1.0, 0.0);
        for (size_t k = 1; k < half; ++k)
            tw[k] = (k % 64 == 0) ? std::polar(1.0, ang * static_cast<double>(k)) : tw[k - 1] * wlen;
        for (size_t i = 0; i < n; i += len)
        {
            for (size_t k = 0; k < half; ++k)
            {
                const cd u = a[i + k];
                const cd v = a[i + k + half] * tw[k];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
        }
    }
    if (inverse)
    {
        const double inv = 1.0 / static_cast<double>(n);
        for (auto& x : a)
            x *= inv;
    }
}

size_t nextPow2(size_t v)
{
    size_t p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

/** Value at the vertex of the parabola through v[i-1], v[i], v[i+1] (sub-sample minimum). */
double parabolicMinValue(const std::vector<double>& v, size_t i)
{
    if (i == 0 || i + 1 >= v.size())
        return v[i];
    const double a = v[i - 1], b = v[i], c = v[i + 1];
    const double denom = a - 2.0 * b + c;
    if (denom <= 1e-20)
        return b;
    return std::max(0.0, b - (a - c) * (a - c) / (8.0 * denom));
}

bool isLocalMin(const std::vector<double>& v, size_t i)
{
    return i > 0 && i + 1 < v.size() && v[i] <= v[i - 1] && v[i] <= v[i + 1];
}

struct FrameResult
{
    bool voiced = false;
    double f0 = 0.0;
    double aperiodicity = 1.0;
};

/**
 * YIN on one frame x[0 .. W + maxLag + 1].
 *  1. difference function d(tau) = sum_j (x_j - x_{j+tau})^2 (via FFT autocorrelation)
 *  2. cumulative mean normalised difference d'(tau)
 *  3. absolute threshold: first dip below threshold, walked to its local minimum
 *     (smallest lag = guards against picking sub-harmonics / octave-low errors)
 *  4. octave-high guard: if d'(2·tau) is far deeper than d'(tau), take 2·tau
 *  5. parabolic interpolation of d around the chosen lag
 */
FrameResult yinFrame(const std::vector<double>& x, size_t W, size_t minLag, size_t maxLag,
                     double threshold, double sampleRate, std::vector<cd>& bufA, std::vector<cd>& bufX)
{
    FrameResult out;
    const size_t N = x.size(); // == W + maxLag + 2
    const size_t lagEnd = maxLag + 1; // we compute d up to maxLag+1 for interpolation
    const size_t M = nextPow2(N);

    bufA.assign(M, cd(0.0, 0.0));
    bufX.assign(M, cd(0.0, 0.0));
    for (size_t i = 0; i < W; ++i)
        bufA[i] = cd(x[i], 0.0);
    for (size_t i = 0; i < N; ++i)
        bufX[i] = cd(x[i], 0.0);
    fft(bufA, false);
    fft(bufX, false);
    for (size_t i = 0; i < M; ++i)
        bufX[i] = std::conj(bufA[i]) * bufX[i];
    fft(bufX, true); // bufX[tau].real() = sum_{j<W} x[j] x[j+tau]

    std::vector<double> prefix(N + 1, 0.0);
    for (size_t i = 0; i < N; ++i)
        prefix[i + 1] = prefix[i] + x[i] * x[i];

    const double e0 = prefix[W];
    std::vector<double> d(lagEnd + 1, 0.0);
    for (size_t tau = 0; tau <= lagEnd; ++tau)
    {
        const double et = prefix[tau + W] - prefix[tau];
        d[tau] = std::max(0.0, e0 + et - 2.0 * bufX[tau].real());
    }

    std::vector<double> dp(lagEnd + 1, 1.0);
    double running = 0.0;
    for (size_t tau = 1; tau <= lagEnd; ++tau)
    {
        running += d[tau];
        dp[tau] = running > 1e-20 ? d[tau] * static_cast<double>(tau) / running : 1.0;
    }

    // Absolute threshold → smallest lag whose dip goes below threshold
    // (For very high notes the true period falls between integer lags, so a dip is also
    // accepted when its interpolated minimum is under the threshold.)
    size_t best = 0;
    for (size_t tau = minLag; tau <= maxLag; ++tau)
    {
        if (dp[tau] < threshold || (isLocalMin(dp, tau) && parabolicMinValue(dp, tau) < threshold))
        {
            while (tau + 1 <= maxLag && dp[tau + 1] < dp[tau])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best == 0)
    {
        // No dip under threshold: global minimum, but prefer the smallest-lag local
        // minimum that is nearly as deep (avoids jumping to sub-harmonics).
        size_t gmin = minLag;
        for (size_t tau = minLag; tau <= maxLag; ++tau)
            if (dp[tau] < dp[gmin])
                gmin = tau;
        best = gmin;
        const double tol = dp[gmin] + 0.03;
        for (size_t tau = std::max<size_t>(minLag, 1); tau < gmin; ++tau)
        {
            if (dp[tau] <= tol && dp[tau] <= dp[tau - 1] && dp[tau] <= dp[tau + 1])
            {
                best = tau;
                break;
            }
        }
    }

    // Octave-high guard: a strong 2nd harmonic can create a shallow dip at T/2.
    // If the dip at ~2·best is dramatically deeper, the true period is 2·best.
    // Compare interpolated minima so integer-lag sampling of short periods doesn't fool it.
    const double bestVal = parabolicMinValue(dp, best);
    if (bestVal > 0.02)
    {
        const size_t lo2 = 2 * best > 2 ? 2 * best - 2 : 1;
        const size_t hi2 = std::min(maxLag, 2 * best + 2);
        size_t b2 = 0;
        for (size_t tau = lo2; tau <= hi2; ++tau)
            if (b2 == 0 || dp[tau] < dp[b2])
                b2 = tau;
        if (b2 != 0 && parabolicMinValue(dp, b2) < 0.25 * bestVal)
            best = b2;
    }

    out.aperiodicity = std::clamp(parabolicMinValue(dp, best), 0.0, 1.0);

    // Parabolic interpolation on the raw difference function
    double refined = static_cast<double>(best);
    if (best >= 1 && best + 1 <= lagEnd)
    {
        const double a = d[best - 1], b = d[best], c = d[best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-20)
        {
            const double delta = std::clamp(0.5 * (a - c) / denom, -1.0, 1.0);
            refined += delta;
        }
    }
    if (refined <= 0.0)
        return out;

    out.f0 = sampleRate / refined;
    out.voiced = true;
    return out;
}

} // namespace

double PitchDetector::hzToMidi(double hz)
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

PitchAnalysis PitchDetector::analyzeInterleaved(const float* interleaved, std::size_t numFrames,
                                                int numChannels, double sampleRate,
                                                const PitchDetectorConfig& cfg)
{
    PitchAnalysis res;
    res.analysed = true;

    if (interleaved == nullptr || numFrames == 0 || numChannels <= 0 || !(sampleRate > 0.0))
    {
        res.reason = "no audio";
        return res;
    }
    if (numFrames < 64)
    {
        res.reason = "too short";
        return res;
    }

    // --- Mix to mono (double), sanitise NaN/Inf, remove global DC ---
    std::vector<double> mono(numFrames, 0.0);
    const double chScale = 1.0 / static_cast<double>(numChannels);
    for (size_t i = 0; i < numFrames; ++i)
    {
        double s = 0.0;
        const float* f = interleaved + i * static_cast<size_t>(numChannels);
        for (int c = 0; c < numChannels; ++c)
        {
            const double v = static_cast<double>(f[c]);
            if (std::isfinite(v))
                s += v;
        }
        mono[i] = s * chScale;
    }
    double mean = 0.0;
    for (double v : mono)
        mean += v;
    mean /= static_cast<double>(numFrames);
    double peak = 0.0;
    for (auto& v : mono)
    {
        v -= mean;
        peak = std::max(peak, std::abs(v));
    }

    const double silenceLin = std::pow(10.0, cfg.silenceDb / 20.0);
    if (peak < silenceLin)
    {
        res.reason = "silent";
        return res;
    }

    // --- Lag range ---
    const double minHz = std::max(1.0, cfg.minHz);
    const double maxHz = std::min(cfg.maxHz, sampleRate * 0.45);
    size_t maxLag = static_cast<size_t>(std::ceil(sampleRate / minHz));
    size_t minLag = std::max<size_t>(2, static_cast<size_t>(std::floor(sampleRate / maxHz)) - 1);
    size_t W = maxLag;

    // Short files: shrink the lowest detectable pitch to fit what we have.
    if (numFrames < W + maxLag + 2)
    {
        maxLag = (numFrames - 2) / 2;
        W = numFrames - 2 - maxLag;
        if (maxLag < minLag * 2 || W < 32)
        {
            res.reason = "too short";
            return res;
        }
    }
    const size_t N = W + maxLag + 2;

    // --- Onset + attack skip ---
    size_t onset = 0;
    const double onsetLevel = peak * 0.1; // -20 dB of peak
    while (onset < numFrames && std::abs(mono[onset]) < onsetLevel)
        ++onset;
    const size_t skip = static_cast<size_t>(cfg.skipAfterOnsetMs * 0.001 * sampleRate);
    size_t start = onset + skip;
    if (start + N > numFrames)
    {
        // Not enough steady audio after the attack: back off towards (or before) the onset.
        start = numFrames >= N ? numFrames - N : 0;
        start = std::max(start, std::min(onset, numFrames - N));
    }

    size_t regionEnd = std::min(numFrames,
                                start + static_cast<size_t>(cfg.maxAnalysisSec * sampleRate));
    regionEnd = std::max(regionEnd, start + N);
    const size_t span = regionEnd - start - N; // room for frame starts
    const int maxFrames = std::max(1, cfg.maxFrames);
    const size_t hopMin = std::max<size_t>(1, N / 4);
    const int frameCount = static_cast<int>(std::min<size_t>(static_cast<size_t>(maxFrames),
                                                             1 + span / hopMin));

    // Level gate: ignore frames that have decayed > 40 dB below peak.
    const double gateRms = peak * 0.01;

    std::vector<double> frame(N);
    std::vector<cd> bufA, bufX;
    std::vector<double> f0s, aps;
    std::vector<FrameResult> results;

    for (int fi = 0; fi < frameCount; ++fi)
    {
        const size_t pos = start + (frameCount > 1 ? span * static_cast<size_t>(fi)
                                                         / static_cast<size_t>(frameCount - 1)
                                                   : 0);
        double fm = 0.0;
        for (size_t i = 0; i < N; ++i)
        {
            frame[i] = mono[pos + i];
            fm += frame[i];
        }
        fm /= static_cast<double>(N);
        double energy = 0.0;
        for (auto& v : frame)
        {
            v -= fm; // local DC removal
            energy += v * v;
        }
        const double rms = std::sqrt(energy / static_cast<double>(N));
        if (rms < gateRms || rms < silenceLin * 0.5)
            continue;

        ++res.framesAnalysed;
        auto fr = yinFrame(frame, W, minLag, maxLag, cfg.yinThreshold, sampleRate, bufA, bufX);
        if (fr.voiced && fr.aperiodicity < 0.35 && fr.f0 >= minHz * 0.97 && fr.f0 <= maxHz * 1.03)
        {
            results.push_back(fr);
            f0s.push_back(std::log2(fr.f0));
            aps.push_back(fr.aperiodicity);
        }
    }

    if (res.framesAnalysed == 0)
    {
        res.reason = "too quiet";
        return res;
    }
    if (f0s.empty())
    {
        res.reason = "no periodicity (noise/percussive)";
        return res;
    }

    const double medLog = median(f0s);
    // Frame agreement: voiced frames within ±50 cents of the median.
    std::vector<double> agreeLogs;
    std::vector<double> agreeAps;
    for (size_t i = 0; i < f0s.size(); ++i)
    {
        if (std::abs(f0s[i] - medLog) * 1200.0 <= 50.0)
        {
            agreeLogs.push_back(f0s[i]);
            agreeAps.push_back(aps[i]);
        }
    }
    res.framesVoiced = static_cast<int>(agreeLogs.size());

    const double f0 = std::pow(2.0, median(agreeLogs));
    const double medAp = median(agreeAps);
    const double periodicity = std::clamp(1.0 - 2.0 * medAp, 0.0, 1.0);
    const double agreement = static_cast<double>(agreeLogs.size())
                           / static_cast<double>(res.framesAnalysed);

    res.f0Hz = f0;
    const double midiF = hzToMidi(f0);
    res.midiNote = static_cast<int>(std::lround(midiF));
    res.cents = static_cast<float>((midiF - static_cast<double>(res.midiNote)) * 100.0);
    res.confidence = static_cast<float>(std::clamp(periodicity * agreement, 0.0, 1.0));
    res.unpitched = res.confidence < cfg.unpitchedBelowConfidence
                 || res.midiNote < 0 || res.midiNote > 127;
    res.reason = res.unpitched ? "low periodicity" : "ok";
    return res;
}

} // namespace looper
