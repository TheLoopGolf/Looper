#pragma once

#include "FilenameTokens.h"
#include "PitchDetector.h"
#include "../InstrumentMap/InstrumentMap.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace looper {

// PitchSource lives in InstrumentMap.h (persisted per sample in .looper.json).

/** Confident audio detection that disagrees with the filename note (filename still wins). */
struct PitchMismatch
{
    int filenameNote = 60;
    int detectedNote = 60;
    int semitones = 0;          // detected - filename (negative = audio sounds lower)
    float confidence = 0.0f;
    bool octave = false;        // |semitones| is a whole number of octaves
    std::string text;           // "Filename C4, audio sounds E4" / "... an octave lower (C3)"
};

struct SampleReview
{
    std::string sampleId;
    PitchSource source = PitchSource::Unpitched;
    float confidence = 0.0f;
    std::vector<std::string> warnings;
    FilenameTokens tokens;
    int rootKey = 60;                    // root actually used for the zone
    float tuneCents = 0.0f;              // fine-tune written to the zone (−detected cents)
    std::optional<PitchAnalysis> pitch;  // YIN result when audio was analysed
    std::optional<PitchMismatch> mismatch;
    /** "Use detected" applied: detection replaced a filename note. */
    bool filenameOverridden = false;
    std::optional<int> filenameNote;     // note parsed from the filename, if any
    /** Unpitched sample placed on its own drum key by the chromatic fallback. */
    bool chromaticKey = false;
};

/** Sample id → YIN analysis of its decoded audio (filled by ImportController / tests). */
using PitchAnalysisMap = std::map<std::string, PitchAnalysis>;

struct AutoMapOptions
{
    /** true = C4 is MIDI 60 (scientific, default); false = C3 is MIDI 60 (filename parse + labels). */
    bool middleCIsC4 = true;
    bool preferFullKeyboardSpan = true;
    bool cycleRrDefault = true;
    /** Write −(detected cents) to Zone::tuneCents so detuned samples play in tune. */
    bool applyDetectedFineTune = true;
    /** Detected roots below this confidence still map, but get a review warning. */
    float lowConfidenceWarnBelow = 0.8f;

    /**
     * Settings → Mapping "No clear pitch" fallback for samples with no filename note and
     * no confident detection.
     *  - Chromatic (default): each sound gets its own single key, consecutive from
     *    unpitchedStartNote (drum-kit style), natural filename order, skipping keys used by
     *    pitched roots; velocity layers / RR alternates of one sound share the key.
     *  - FixedRoot: legacy "equal spread + warn" — root middle C (MIDI 60), key span shared
     *    with the pitched roots (full range when alone), review warning per sample.
     */
    enum class UnpitchedFallback { Chromatic, FixedRoot, SpreadWarn = FixedRoot };
    UnpitchedFallback unpitchedFallback = UnpitchedFallback::Chromatic;
    int unpitchedStartNote = 36;         // MIDI 36 = GM kick (C2 in C4=60 naming, C1 in C3=60)

    /** Filename/audio mismatch flag: detection at/above this confidence, >= 1 semitone apart. */
    float mismatchMinConfidence = 0.8f;
    /** Sample ids whose confident detection should replace the filename note ("Use detected"). */
    std::set<std::string> useDetectedFor;
};

// Inline so JUCE-free libs that only include this header (SessionPrefs) need not link AutoMapper.
inline const char* unpitchedFallbackToString(AutoMapOptions::UnpitchedFallback f)
{
    return f == AutoMapOptions::UnpitchedFallback::FixedRoot ? "fixedRoot" : "chromatic";
}

/** "chromatic" / "fixedRoot" (legacy "spreadWarn" / "equalSpread" accepted). */
inline std::optional<AutoMapOptions::UnpitchedFallback> unpitchedFallbackFromString(const std::string& s)
{
    if (s == "chromatic") return AutoMapOptions::UnpitchedFallback::Chromatic;
    if (s == "fixedRoot" || s == "spreadWarn" || s == "equalSpread")
        return AutoMapOptions::UnpitchedFallback::FixedRoot;
    return std::nullopt;
}

struct AutoMapResult
{
    InstrumentMap map;
    std::vector<SampleReview> reviews;
    std::vector<std::string> globalWarnings;
    /** Naming convention the map was built with (labels in the Review screen follow it). */
    bool middleCIsC4 = true;
};

class AutoMapper
{
public:
    /**
     * Deterministic: same samples + options (+ analyses) → same map.
     * Root key priority: filename note token → detected pitch (analyses[id], if pitched)
     * → unpitched fallback. Pass nullptr when no audio was analysed. Analyses of samples
     * WITH a filename note are only used for the mismatch flag (and "Use detected").
     */
    static AutoMapResult map(const std::vector<SampleRef>& samples,
                             AutoMapOptions opt = {},
                             const PitchAnalysisMap* analyses = nullptr);

    /** True when this review can switch to its detected note ("Use detected"). */
    static bool canUseDetected(const SampleReview& review);
};

/**
 * Copy per-sample pitch metadata (source, confidence, cents, Hz, detected root, mismatch)
 * from map reviews into the refs that will be saved in .looper.json.
 */
void applyPitchMetadata(std::vector<SampleRef>& refs, const AutoMapResult& result);

/**
 * One-line pitch summary for the review table, UTF-8, e.g.
 *   "Detected C#3 (−12 ct) 92%", "Filename C4", "Unpitched (noise/percussive) → C2".
 */
std::string describePitch(const SampleReview& review, bool middleCIsC4 = true);

/** "an octave lower", "2 octaves higher", "3 semitones higher" for a semitone delta. */
std::string describeInterval(int semitones);

} // namespace looper
