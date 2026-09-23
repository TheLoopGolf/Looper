#pragma once

#include "../InstrumentMap/InstrumentMap.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace looper {

struct SampleBuffer
{
    std::vector<float> interleaved; // mono or stereo interleaved
    int channels = 1;
    double sampleRate = 44100.0;
    int64_t length = 0; // frames
};

inline constexpr const char* kDemoSampleId = "demo";

/**
 * ~0.4 s band-limited-ish decaying sine at C4 (261.6256 Hz), mono.
 * Used so Standalone makes sound without external files.
 */
inline SampleBuffer makeDemoToneBuffer(double sampleRate = 44100.0, double durationSec = 0.4)
{
    SampleBuffer buf;
    buf.channels = 1;
    buf.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int64_t n = static_cast<int64_t>(std::max(1.0, buf.sampleRate * durationSec));
    buf.length = n;
    buf.interleaved.resize(static_cast<size_t>(n));

    constexpr double kC4Hz = 261.6255653;
    const double twoPi = 6.283185307179586;
    // Soft attack (~3 ms) + exponential-ish decay envelope on the tone itself
    const double attackFrames = std::max(1.0, buf.sampleRate * 0.003);

    for (int64_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / buf.sampleRate;
        const double phase = twoPi * kC4Hz * t;
        // Simple band-limit-ish: fundamental + weak 2nd/3rd harmonics below Nyquist
        double s = std::sin(phase);
        s += 0.25 * std::sin(2.0 * phase);
        s += 0.08 * std::sin(3.0 * phase);
        const double attack = std::min(1.0, static_cast<double>(i) / attackFrames);
        const double decay = std::exp(-4.5 * t / durationSec);
        buf.interleaved[static_cast<size_t>(i)] = static_cast<float>(0.35 * s * attack * decay);
    }
    return buf;
}

/**
 * Sample pool — RAM buffers keyed by sample id.
 * Stays mostly JUCE-free; PluginProcessor may decode WAV and call setBuffer.
 */
class SamplePool
{
public:
    bool contains(const std::string& id) const { return buffers_.count(id) > 0; }

    void setBuffer(const std::string& id, SampleBuffer buffer)
    {
        auto sp = std::make_shared<SampleBuffer>(std::move(buffer));
        if (sp->length <= 0 && !sp->interleaved.empty())
            sp->length = static_cast<int64_t>(sp->interleaved.size() / static_cast<size_t>(std::max(1, sp->channels)));
        buffers_[id] = std::move(sp);
    }

    void setBuffer(const std::string& id, std::shared_ptr<SampleBuffer> buffer)
    {
        if (!buffer)
            return;
        if (buffer->length <= 0 && !buffer->interleaved.empty())
            buffer->length = static_cast<int64_t>(buffer->interleaved.size() / static_cast<size_t>(std::max(1, buffer->channels)));
        buffers_[id] = std::move(buffer);
    }

    std::shared_ptr<const SampleBuffer> getBuffer(const std::string& id) const
    {
        const auto it = buffers_.find(id);
        if (it == buffers_.end())
            return nullptr;
        return it->second;
    }

    SampleRef* addStub(SampleRef ref)
    {
        auto id = ref.id;
        refs_[id] = std::move(ref);
        return &refs_[id];
    }

    const SampleRef* getRef(const std::string& id) const
    {
        const auto it = refs_.find(id);
        return it == refs_.end() ? nullptr : &it->second;
    }

    /** Install demo tone buffer under kDemoSampleId (overwrites if present). */
    void loadDemoSample(double sampleRate = 44100.0)
    {
        auto buf = makeDemoToneBuffer(sampleRate);
        SampleRef ref;
        ref.id = kDemoSampleId;
        ref.path = "demo://tone-c4";
        ref.displayName = "Demo C4 Tone";
        ref.durationSamples = buf.length;
        ref.sampleRate = buf.sampleRate;
        ref.channels = buf.channels;
        ref.detectedRootKey = 60;
        addStub(std::move(ref));
        setBuffer(kDemoSampleId, std::move(buf));
    }

    void clear()
    {
        refs_.clear();
        buffers_.clear();
    }

private:
    std::unordered_map<std::string, SampleRef> refs_;
    std::unordered_map<std::string, std::shared_ptr<SampleBuffer>> buffers_;
};

/** Single full-range zone pointing at the demo sample (root C4=60). */
inline Zone makeDemoZone()
{
    Zone z;
    z.sampleId = kDemoSampleId;
    z.rootKey = 60;
    z.keyLow = 0;
    z.keyHigh = 127;
    z.velLow = 1;
    z.velHigh = 127;
    return z;
}

} // namespace looper
