#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace looper {

struct SampleRef
{
    std::string id;
    std::string path;
    std::string displayName;
    std::optional<std::string> fileHash;
    std::optional<int64_t> durationSamples;
    std::optional<double> sampleRate;
    std::optional<int> channels;
    std::optional<double> detectedPitchHz;
    std::optional<int> detectedRootKey;
    std::optional<int64_t> loopStart;
    std::optional<int64_t> loopEnd;
};

struct Zone
{
    std::string sampleId;
    int rootKey = 60;
    int keyLow = 0;
    int keyHigh = 127;
    int velLow = 1;
    int velHigh = 127;
    int rrGroup = 0;
    int rrIndex = 0;
    float tuneCents = 0.0f;
    int coarseTranspose = 0;
    float gainDb = 0.0f;
    float pan = 0.0f;
    std::optional<int64_t> sampleStart;
    std::optional<int64_t> sampleEnd;

    bool matchesNoteVelocity(int note, int velocity) const
    {
        return note >= keyLow && note <= keyHigh && velocity >= velLow && velocity <= velHigh;
    }
};

enum class VelCurve
{
    Linear,
    Soft,
    Hard
};

enum class ModWheelTarget
{
    FilterCutoff,
    Volume
};

struct InstrumentMap
{
    std::vector<Zone> zones;
    float volumeDb = 0.0f;
    int polyphonyLimit = 64;
    float glideMs = 0.0f;
    VelCurve velCurve = VelCurve::Linear;
    ModWheelTarget modWheelTarget = ModWheelTarget::FilterCutoff;

    /** Indices of zones matching note + velocity, in map order (stable). */
    std::vector<size_t> matchingZoneIndices(int note, int velocity) const
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < zones.size(); ++i)
        {
            if (zones[i].matchesNoteVelocity(note, velocity))
                out.push_back(i);
        }
        return out;
    }
};

} // namespace looper
