#pragma once

#include "../InstrumentMap/InstrumentMap.h"
#include "../AutoMapper/AutoMapper.h"

#include <optional>
#include <string>

namespace looper {

/**
 * Global / session preferences (Settings screen).
 * Persisted in host state; mapping fields also feed the next AutoMapper import.
 * Performance knobs (ADSR / filter) stay on MainView / APVTS.
 */
struct SessionPrefs
{
    // --- Engine ---
    int polyphony = 64;                 // 1..128
    float glideMs = 0.0f;               // 0 = off; stored on map; VoiceEngine legato portamento
    bool masterSoftClip = false;        // applied at voice-sum output when true
    int defaultFilterType = 0;          // 0=LP, 1=HP, 2=BP → APVTS filterType

    // --- Mapping (next AutoMapper import + map display defaults) ---
    bool middleCIsC4 = true;            // C4=60 default; false = C3=60 (filename parse + note labels)
    bool preferFullKeyboardSpan = true;
    bool cycleRrDefault = true;         // Random stubbed / disabled in UI
    VelCurve velCurve = VelCurve::Linear;
    bool openReviewAfterImport = true;
    /** "No clear pitch" fallback (Settings -> Mapping). Chromatic drum keys by default. */
    AutoMapOptions::UnpitchedFallback unpitchedFallback = AutoMapOptions::UnpitchedFallback::Chromatic;
    int unpitchedStartNote = 36;        // MIDI note of the first drum key (GM kick)

    // --- Sample memory / disk streaming ---
    /**
     * Frames of each sample kept in RAM (the preload); the rest streams from disk. Samples no
     * longer than this stay fully in RAM. Per-patch "Load fully into RAM" overrides it.
     */
    int preloadFrames = kDefaultPreloadFrames;
    static constexpr int kDefaultPreloadFrames = 65536;
    static constexpr int kMinPreloadFrames = 8192;
    static constexpr int kMaxPreloadFrames = 1048576;

    // --- MIDI ---
    float pitchBendRangeSemis = 2.0f;
    ModWheelTarget modWheelTarget = ModWheelTarget::FilterCutoff;

    AutoMapOptions toAutoMapOptions() const
    {
        AutoMapOptions o;
        o.middleCIsC4 = middleCIsC4;
        o.preferFullKeyboardSpan = preferFullKeyboardSpan;
        o.cycleRrDefault = cycleRrDefault;
        o.unpitchedFallback = unpitchedFallback;
        o.unpitchedStartNote = unpitchedStartNote;
        return o;
    }

    /** Deterministic JSON for host state / unit tests (no JUCE). */
    std::string toJson() const;
    static std::optional<SessionPrefs> fromJson(const std::string& json);

    bool approximatelyEqual(const SessionPrefs& o) const
    {
        auto near = [](float a, float b) { return (a > b ? a - b : b - a) < 1.0e-4f; };
        return polyphony == o.polyphony
            && near(glideMs, o.glideMs)
            && masterSoftClip == o.masterSoftClip
            && defaultFilterType == o.defaultFilterType
            && middleCIsC4 == o.middleCIsC4
            && preferFullKeyboardSpan == o.preferFullKeyboardSpan
            && cycleRrDefault == o.cycleRrDefault
            && velCurve == o.velCurve
            && openReviewAfterImport == o.openReviewAfterImport
            && unpitchedFallback == o.unpitchedFallback
            && unpitchedStartNote == o.unpitchedStartNote
            && preloadFrames == o.preloadFrames
            && near(pitchBendRangeSemis, o.pitchBendRangeSemis)
            && modWheelTarget == o.modWheelTarget;
    }
};

} // namespace looper
