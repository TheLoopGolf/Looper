#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace looper {

/**
 * Where a sample's root key came from (AutoMapper + .looper.json "pitchSource").
 * Spread is the pre-v1.1 name for Unpitched and is kept as an alias for source compatibility.
 */
enum class PitchSource
{
    Filename,
    Detected,
    Unpitched,
    Spread = Unpitched
};

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
    // --- Pitch-detection metadata (all optional; absent in pre-v1.1 patches) ---
    /** Source of the root key used at import ("filename" / "detected" / "unpitched"). */
    std::optional<PitchSource> pitchSource;
    /** YIN confidence 0..1 whenever the audio was analysed (also for filename-named samples). */
    std::optional<float> pitchConfidence;
    /** Detected offset of the audio from detectedRootKey in cents (-50..+50). */
    std::optional<float> detectedCents;
    /** Confident detection disagrees with the filename note by >= 1 semitone. */
    bool pitchMismatch = false;
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

/** Lightweight key-range span for UI zone visualization. */
struct ZoneKeySpan
{
    int low = 0;
    int high = 127;
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

/**
 * How a round-robin group (zones sharing note + velocity layer + rrGroup) picks its next
 * alternate. Cycle = rrIndex order (default, v1 behaviour). Random = uniform choice that
 * never repeats the previously played alternate when the group has 2+ alternates.
 */
enum class RoundRobinMode
{
    Cycle = 0,
    Random = 1
};

struct InstrumentMap
{
    std::vector<Zone> zones;
    float volumeDb = 0.0f;
    int polyphonyLimit = 64;
    float glideMs = 0.0f;
    VelCurve velCurve = VelCurve::Linear;
    ModWheelTarget modWheelTarget = ModWheelTarget::FilterCutoff;
    /** Per-patch RR mode (.looper.json "roundRobinMode"; missing = Cycle). */
    RoundRobinMode rrMode = RoundRobinMode::Cycle;

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
