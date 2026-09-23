#pragma once

#include <algorithm>
#include <cmath>

namespace looper {

enum class FilterType
{
    LowPass,
    HighPass,
    BandPass
};

/**
 * Linear SVF (Andy Simper / Chamberlin-style trapezoidal).
 * Shared coefficients; dual state for independent L/R processing.
 * Resonance 0..1 maps to Q ≈ 0.5..~8.5 with a soft high-end limit.
 */
class SvfFilter
{
public:
    void setSampleRate(double sr)
    {
        sampleRate_ = sr > 0.0 ? sr : 44100.0;
        dirty_ = true;
    }

    void setType(FilterType t) { type_ = t; }

    void setCutoffHz(float hz)
    {
        cutoffHz_ = hz;
        dirty_ = true;
    }

    /** Resonance control 0..1 (not raw Q). */
    void setResonance(float r01)
    {
        resonance_ = std::clamp(r01, 0.0f, 1.0f);
        dirty_ = true;
    }

    void reset()
    {
        ic1eqL_ = ic2eqL_ = 0.0f;
        ic1eqR_ = ic2eqR_ = 0.0f;
    }

    /** Map resonance 0..1 → Q with soft limit to avoid blow-ups. */
    static float resonanceToQ(float r01)
    {
        r01 = std::clamp(r01, 0.0f, 1.0f);
        // Linear aim 0.5..10, soft-limited: max ≈ 8.33 at r=1
        return 0.5f + 9.5f * r01 / (1.0f + 0.2f * r01);
    }

    float process(float x)
    {
        updateCoeffs();
        float lp = 0, bp = 0, hp = 0;
        tick(x, ic1eqL_, ic2eqL_, lp, bp, hp);
        return select(lp, bp, hp);
    }

    void processStereo(float& left, float& right)
    {
        updateCoeffs();
        float lpL = 0, bpL = 0, hpL = 0;
        float lpR = 0, bpR = 0, hpR = 0;
        tick(left, ic1eqL_, ic2eqL_, lpL, bpL, hpL);
        tick(right, ic1eqR_, ic2eqR_, lpR, bpR, hpR);
        left = select(lpL, bpL, hpL);
        right = select(lpR, bpR, hpR);
    }

    FilterType type() const { return type_; }
    float cutoffHz() const { return cutoffHz_; }
    float resonance() const { return resonance_; }

private:
    void updateCoeffs()
    {
        if (!dirty_)
            return;
        dirty_ = false;

        const float sr = static_cast<float>(sampleRate_);
        const float nyquistLimit = 0.49f * sr;
        const float fc = std::clamp(cutoffHz_, 20.0f, nyquistLimit);
        const float q = std::max(0.1f, resonanceToQ(resonance_));

        // g = tan(pi * fc / sr); clamp argument for safety near Nyquist
        const float g = std::tan(static_cast<float>(3.14159265358979323846) * fc / sr);
        const float k = 1.0f / q;
        a1_ = 1.0f / (1.0f + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
        k_ = k;
    }

    void tick(float v0, float& ic1eq, float& ic2eq, float& lp, float& bp, float& hp) const
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1_ * ic1eq + a2_ * v3;
        const float v2 = ic2eq + a2_ * ic1eq + a3_ * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        // Flush denormals
        if (ic1eq > -1.0e-15f && ic1eq < 1.0e-15f)
            ic1eq = 0.0f;
        if (ic2eq > -1.0e-15f && ic2eq < 1.0e-15f)
            ic2eq = 0.0f;

        lp = v2;
        bp = v1;
        hp = v0 - k_ * v1 - v2;
    }

    float select(float lp, float bp, float hp) const
    {
        switch (type_)
        {
            case FilterType::LowPass: return lp;
            case FilterType::HighPass: return hp;
            case FilterType::BandPass: return bp;
        }
        return lp;
    }

    double sampleRate_ = 44100.0;
    FilterType type_ = FilterType::LowPass;
    float cutoffHz_ = 12000.0f;
    float resonance_ = 0.1f;
    bool dirty_ = true;

    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f, k_ = 0.0f;
    float ic1eqL_ = 0.0f, ic2eqL_ = 0.0f;
    float ic1eqR_ = 0.0f, ic2eqR_ = 0.0f;
};

} // namespace looper
