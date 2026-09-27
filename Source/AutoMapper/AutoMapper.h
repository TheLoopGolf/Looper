#pragma once

#include "FilenameTokens.h"
#include "PitchDetector.h"
#include "../InstrumentMap/InstrumentMap.h"

#include <map>
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
    int rootKey = 60;                    // root actually used for the zone
    float tuneCents = 0.0f;              // fine-tune written to the zone (−detected cents)
    std::optional<PitchAnalysis> pitch;  // YIN result when audio was analysed
};

/** Sample id → YIN analysis of its decoded audio (filled by ImportController / tests). */
using PitchAnalysisMap = std::map<std::string, PitchAnalysis>;

struct AutoMapOptions
{
    /** true = C4 is MIDI 60 (scientific); false reserved for C3=60 later */
    bool middleCIsC4 = true;
    bool preferFullKeyboardSpan = true;
    bool cycleRrDefault = true;
    /** Write −(detected cents) to Zone::tuneCents so detuned samples play in tune. */
    bool applyDetectedFineTune = true;
    /** Detected roots below this confidence still map, but get a review warning. */
    float lowConfidenceWarnBelow = 0.8f;
    /** Unpitched fallback (Settings → Mapping). Only "equal spread + warn" exists in v1. */
    enum class UnpitchedFallback { SpreadWarn };
    UnpitchedFallback unpitchedFallback = UnpitchedFallback::SpreadWarn;
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
    /**
     * Deterministic: same samples + options (+ analyses) → same map.
     * Root key priority: filename note token → detected pitch (analyses[id], if pitched)
     * → unpitched fallback. Pass nullptr when no audio was analysed.
     */
    static AutoMapResult map(const std::vector<SampleRef>& samples,
                             AutoMapOptions opt = {},
                             const PitchAnalysisMap* analyses = nullptr);
};

/**
 * One-line pitch summary for the review table, UTF-8, e.g.
 *   "Detected C#3 (−12 ct) 92%", "Filename C4", "Unpitched (noise/percussive) → C4".
 */
std::string describePitch(const SampleReview& review);

} // namespace looper
