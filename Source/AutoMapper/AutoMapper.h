#pragma once

#include "FilenameTokens.h"
#include "../InstrumentMap/InstrumentMap.h"

#include <optional>
#include <string>
#include <vector>

namespace looper {

enum class PitchSource
{
    Filename,
    Detected,
    Spread
};

struct SampleReview
{
    std::string sampleId;
    PitchSource source = PitchSource::Spread;
    float confidence = 0.0f;
    std::vector<std::string> warnings;
    FilenameTokens tokens;
};

struct AutoMapOptions
{
    /** true = C4 is MIDI 60 (scientific); false reserved for C3=60 later */
    bool middleCIsC4 = true;
    bool preferFullKeyboardSpan = true;
    bool cycleRrDefault = true;
};

struct PitchDetectResult
{
    std::optional<double> pitchHz;
    std::optional<int> rootKey;
    float confidence = 0.0f;
};

struct AutoMapResult
{
    InstrumentMap map;
    std::vector<SampleReview> reviews;
    std::vector<std::string> globalWarnings;
};

class AutoMapper
{
public:
    /** Deterministic: same samples + options → same map. */
    static AutoMapResult map(const std::vector<SampleRef>& samples,
                             AutoMapOptions opt = {});

    /**
     * YIN pitch-detect stub. Always returns null / low confidence.
     * TODO: implement YIN on a center window after onset.
     */
    static PitchDetectResult detectPitchStub(const SampleRef& sample);
};

} // namespace looper
