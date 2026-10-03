#pragma once

#include "../InstrumentMap/InstrumentMap.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace looper {

/**
 * Manual zone editing (main screen zone editor), kept free of JUCE so it can be unit-tested.
 *
 * Rules:
 *  - Keys / root are clamped to 0..127, velocities to 1..127 (0 is note-off in MIDI).
 *  - Ranges never invert: moving a low bound above its high bound drags the high bound
 *    along (and vice versa), so the zone collapses to a single key / velocity at worst.
 *  - Fine tune is clamped to +-100 cents, gain to kMinGainDb..kMaxGainDb.
 *  - Overlaps with other zones are reported (findOverlaps), never blocked.
 * Undo/redo: each change is a ZoneEdit (index + before/after snapshot). The plugin wraps it in
 * a juce::UndoableAction (ZoneEditAction.h) that calls ZoneEditTarget::replaceZone.
 */
enum class ZoneField
{
    RootKey,
    KeyLow,
    KeyHigh,
    VelLow,
    VelHigh,
    TuneCents,
    GainDb,
    RrGroup,
    RrIndex
};

namespace zone_limits {
constexpr int kMinKey = 0;
constexpr int kMaxKey = 127;
constexpr int kMinVel = 1;
constexpr int kMaxVel = 127;
constexpr float kMinTuneCents = -100.0f;
constexpr float kMaxTuneCents = 100.0f;
constexpr float kMinGainDb = -48.0f;
constexpr float kMaxGainDb = 24.0f;
constexpr int kMaxRrGroup = 999;
constexpr int kMaxRrIndex = 127;
} // namespace zone_limits

/** Short UI / undo label, e.g. "Root key", "Fine tune". */
const char* zoneFieldName(ZoneField field);

/** Current value of a field as a double (ints are exact). */
double zoneFieldValue(const Zone& zone, ZoneField field);

/** Inclusive limits of a field (for sliders / validation). */
void zoneFieldRange(ZoneField field, double& minOut, double& maxOut);

/**
 * Clamp every editable field into range and repair inverted key / velocity ranges
 * (an inverted pair is swapped). Returns true if the zone changed.
 */
bool sanitizeZone(Zone& zone);

/**
 * Copy of `zone` with `field` set to `value` (rounded for integer fields, clamped).
 * Low/high partners are pushed so the range never inverts.
 */
Zone withZoneField(const Zone& zone, ZoneField field, double value);

/** True when the editable fields (and sample id) of a and b are identical. */
bool sameZoneSettings(const Zone& a, const Zone& b);

// --- Auto values ("Reset to auto") -----------------------------------------------------

ZoneAutoValues captureAutoValues(const Zone& zone);

/** Record each zone's current fields as its AutoMapper values (called when an import is accepted). */
void stampAutoValues(InstrumentMap& map);

/** Zone with its auto values restored (unchanged copy when it has none). */
Zone resetZoneToAuto(const Zone& zone);

/** Zone carries auto values and at least one editable field differs from them. */
bool isZoneEditedFromAuto(const Zone& zone);

// --- Detected pitch ("Use detected pitch") ---------------------------------------------

struct DetectedPitchInfo
{
    int rootKey = 60;
    float cents = 0.0f;       // audio offset from rootKey (+ = sharp)
    float confidence = 0.0f;  // 0..1 (1 when the patch did not record it)
};

/**
 * Pitch detected for a sample at import, if any. Requires detectedRootKey; unpitched samples
 * (pitchSource Unpitched with no usable detection) return nullopt.
 */
std::optional<DetectedPitchInfo> detectedPitchFor(const SampleRef& sample);

/** Root = detected note, fine tune = -detected cents (so the sample plays in tune). */
Zone withDetectedPitch(const Zone& zone, const DetectedPitchInfo& pitch);

/** Zone already uses the detected root and tune (within 0.05 ct). */
bool zoneUsesDetectedPitch(const Zone& zone, const DetectedPitchInfo& pitch);

// --- Overlaps ----------------------------------------------------------------------------

/**
 * Other zones whose key AND velocity ranges intersect zones[index]. Round-robin alternates
 * (same non-zero rrGroup) are intentional stacks and are not reported.
 */
std::vector<size_t> findOverlaps(const InstrumentMap& map, size_t index);

// --- Edits / undo ---------------------------------------------------------------------------

struct ZoneEdit
{
    size_t index = 0;
    std::string sampleId;  // guards against a stale index after the map was replaced
    Zone before;
    Zone after;
};

/**
 * Build an edit that turns zones[index] into `proposed` (sanitized). nullopt when the index is
 * out of range, the sample id changed, or the result equals the current zone.
 */
std::optional<ZoneEdit> makeZoneEdit(const InstrumentMap& map, size_t index, const Zone& proposed);

/** zones[index] = zone if the index is valid and the sample id matches. */
bool replaceZoneInMap(InstrumentMap& map, size_t index, const std::string& sampleId, const Zone& zone);

/** Apply (forward) or revert an edit on a map. */
bool applyZoneEdit(InstrumentMap& map, const ZoneEdit& edit, bool forward = true);

/** One proposed zone (index into map.zones); a group of them is applied as one undo step. */
struct ZoneChange
{
    size_t index = 0;
    Zone zone;
};

/**
 * Edits for a group of proposed zones (each sanitized), skipping zones that would not change.
 * Empty when nothing changes, or when any index is out of range / its sample id changed
 * (a stale proposal never half-applies).
 */
std::vector<ZoneEdit> makeZoneEdits(const InstrumentMap& map, const std::vector<ZoneChange>& changes);

/**
 * Apply (forward) or revert a group of edits on a map, all-or-nothing: every index / sample id
 * is validated first. Reverting walks the group backwards.
 */
bool applyZoneEdits(InstrumentMap& map, const std::vector<ZoneEdit>& edits, bool forward = true);

/** Receiver of undoable zone edits (the audio processor; a fake in tests). */
class ZoneEditTarget
{
public:
    virtual ~ZoneEditTarget() = default;
    /** Replace zones[index] (guarded by sampleId); must push the change to playback. */
    virtual bool replaceZone(size_t index, const std::string& sampleId, const Zone& zone) = 0;

    /**
     * Apply a group of edits (forward) or revert it as one change. The default calls
     * replaceZone per edit; the processor overrides it to publish a single live map.
     */
    virtual bool replaceZones(const std::vector<ZoneEdit>& edits, bool forward)
    {
        bool ok = true;
        if (forward)
        {
            for (const auto& e : edits)
                ok = replaceZone(e.index, e.sampleId, e.after) && ok;
        }
        else
        {
            for (auto it = edits.rbegin(); it != edits.rend(); ++it)
                ok = replaceZone(it->index, it->sampleId, it->before) && ok;
        }
        return ok;
    }
};

/**
 * Note + velocity that audition exactly this zone: the root key when it lies inside the key
 * range (else the nearest in-range key) and velocity 100 clamped into the zone's layer.
 */
void auditionNoteFor(const Zone& zone, int& noteOut, int& velocityOut);

} // namespace looper
