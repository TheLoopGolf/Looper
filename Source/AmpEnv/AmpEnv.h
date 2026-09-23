#pragma once

#include <algorithm>
#include <cmath>

namespace looper {

/**
 * Linear ADSR envelope (v1 milestone).
 * Min attack 0.1 ms to avoid clicks. process() returns 0 and Idle when finished.
 * Exp curves / steal fade are roadmap; linear is intentional for this milestone.
 */
class AmpEnv
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    struct Params
    {
        float attackMs = 1.0f;
        float decayMs = 100.0f;
        float sustain = 0.8f;
        float releaseMs = 200.0f;
    };

    void setSampleRate(double sr) { sampleRate_ = sr > 0.0 ? sr : 44100.0; }
    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    /** Start attack. If fromCurrent, keep level_ (retrigger); else reset to 0. */
    void noteOn(bool fromCurrent = false)
    {
        if (!fromCurrent)
            level_ = 0.0f;
        releasing_ = false;

        if (level_ >= 1.0f)
        {
            level_ = 1.0f;
            enterDecay();
            return;
        }

        stage_ = Stage::Attack;
        const float ms = std::max(0.1f, params_.attackMs);
        const float samples = std::max(1.0f, float(sampleRate_ * ms * 0.001));
        attackInc_ = (1.0f - level_) / samples;
    }

    void noteOff()
    {
        if (stage_ == Stage::Idle)
            return;
        releasing_ = true;
        stage_ = Stage::Release;
        const float ms = std::max(1.0f, params_.releaseMs);
        const float samples = std::max(1.0f, float(sampleRate_ * ms * 0.001));
        releaseInc_ = level_ / samples; // linear from current level to 0
    }

    /** Advance one sample; returns amplitude 0..1. Idle → 0. */
    float process()
    {
        switch (stage_)
        {
            case Stage::Idle:
                level_ = 0.0f;
                return 0.0f;

            case Stage::Attack:
                level_ += attackInc_;
                if (level_ >= 1.0f)
                {
                    level_ = 1.0f;
                    enterDecay();
                }
                break;

            case Stage::Decay:
                level_ -= decayInc_;
                if (level_ <= params_.sustain)
                {
                    level_ = params_.sustain;
                    stage_ = Stage::Sustain;
                }
                break;

            case Stage::Sustain:
                level_ = params_.sustain;
                break;

            case Stage::Release:
                level_ -= releaseInc_;
                if (level_ <= 0.0f)
                {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                    releasing_ = false;
                }
                break;
        }
        return level_;
    }

    bool isActive() const { return stage_ != Stage::Idle; }
    bool isIdle() const { return stage_ == Stage::Idle; }
    Stage stage() const { return stage_; }
    float level() const { return level_; }

private:
    void enterDecay()
    {
        stage_ = Stage::Decay;
        const float ms = std::max(1.0f, params_.decayMs);
        const float samples = std::max(1.0f, float(sampleRate_ * ms * 0.001));
        const float delta = std::max(0.0f, 1.0f - params_.sustain);
        decayInc_ = delta / samples;
        if (delta <= 0.0f)
        {
            level_ = params_.sustain;
            stage_ = Stage::Sustain;
        }
    }

    Params params_;
    double sampleRate_ = 44100.0;
    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
    bool releasing_ = false;
    float attackInc_ = 0.0f;
    float decayInc_ = 0.0f;
    float releaseInc_ = 0.0f;
};

} // namespace looper
