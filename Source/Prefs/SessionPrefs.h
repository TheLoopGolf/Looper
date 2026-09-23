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
    float glideMs = 0.0f;               // 0 = off; stored on map; VoiceEngine glide TODO
    bool masterSoftClip = false;        // applied at voice-sum output when true
    int defaultFilterType = 0;          // 0=LP, 1=HP, 2=BP → APVTS filterType

    // --- Mapping (next AutoMapper import + map display defaults) ---
    bool middleCIsC4 = true;            // locked C4=60 for v1
    bool preferFullKeyboardSpan = true;
    bool cycleRrDefault = true;         // Random stubbed / disabled in UI
    VelCurve velCurve = VelCurve::Linear;
    bool openReviewAfterImport = true;

    // --- MIDI ---
    float pitchBendRangeSemis = 2.0f;
    ModWheelTarget modWheelTarget = ModWheelTarget::FilterCutoff;

    AutoMapOptions toAutoMapOptions() const
    {
        AutoMapOptions o;
        o.middleCIsC4 = middleCIsC4;
        o.preferFullKeyboardSpan = preferFullKeyboardSpan;
        o.cycleRrDefault = cycleRrDefault;
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
            && near(pitchBendRangeSemis, o.pitchBendRangeSemis)
            && modWheelTarget == o.modWheelTarget;
    }
};

} // namespace looper
