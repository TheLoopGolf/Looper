#pragma once

#include "ZoneSelection.h"

#include <cstddef>
#include <string>
#include <vector>

namespace looper {

/**
 * Keyboard zone strip maths (JUCE-free): key geometry, hit-testing of zone edges / root
 * markers / bodies, key snapping and the zone changes a drag produces. The JUCE component
 * (Source/UI/ZoneKeyboardComponent) only paints and forwards mouse events.
 */
struct KeyStripLayout
{
    int lowKey = 36;     // visible range, inclusive
    int highKey = 96;
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;   // key area

    static bool isBlack (int midi) noexcept;
    int whiteCount() const noexcept;
    float whiteWidth() const noexcept;
    float blackWidth() const noexcept { return whiteWidth() * 0.62f; }
    float blackHeight() const noexcept { return height * 0.62f; }

    /** Absolute left edge / width / horizontal centre of a key (any key, even off-screen). */
    float keyLeft (int midi) const noexcept;
    float keyWidth (int midi) const noexcept;
    float keyCentre (int midi) const noexcept;
    /** Centre of the root marker dot ("ball on the tee") drawn on a key. */
    float rootMarkerY (int midi) const noexcept;

    /** Key under a point (black keys on top), -1 outside the key area. */
    int keyAt (float px, float py) const noexcept;
    /**
     * Snap for drags: the key whose centre is nearest px (chromatic), clamped to the visible
     * range when the pointer leaves the strip sideways.
     */
    int nearestKey (float px) const noexcept;

    /** Horizontal extent of keys low..high clipped to the visible range; empty when off-screen. */
    struct Span
    {
        float x0 = 0.0f, x1 = 0.0f;
        bool visible = false;
    };
    Span spanFor (int low, int high) const noexcept;
};

enum class StripPart
{
    None,
    Body,       // drag = move the zone (width kept)
    LowEdge,    // drag = low key
    HighEdge,   // drag = high key
    Root,       // drag = root key (click = audition)
    DrawRange   // Alt/Option-drag: draw a key range for the selection
};

struct StripHit
{
    int zone = -1;
    StripPart part = StripPart::None;
    int key = -1;   // key under the pointer (-1 outside)
};

/** Pixels either side of a zone edge that grab it (shrinks for narrow zones). */
constexpr float kStripEdgeGrabPx = 5.0f;

/**
 * What a mouse-down at (px, py) grabs. Selected zones win (primary first), then every other
 * zone in map order; within each group: root marker, then edges, then body. Several zones covering the key
 * (velocity layers / RR stacks) resolve to the selected one when it is among them.
 */
StripHit hitTestStrip (const KeyStripLayout& layout, const std::vector<ZoneKeySpan>& zones,
                       const ZoneSelection& selection, float px, float py);

/**
 * Zone changes for a strip drag, always computed from the zones as they were at mouse-down
 * (`origin`) so dragging back restores them and clamping never accumulates.
 *  - LowEdge / HighEdge / Root: the primary's edge / root follows the pointer key; every other
 *    selected zone moves by the same number of keys. Same rules as the editor (0..127,
 *    ranges never invert: an edge pushed past the other drags it along).
 *  - Body: key ranges and roots move by (currentKey - anchorKey), clamped for the whole group
 *    so every zone keeps its width.
 *  - DrawRange: every selected zone gets keys min(anchor, current)..max(anchor, current).
 */
std::vector<ZoneChange> computeStripDrag (const InstrumentMap& origin, const ZoneSelection& selection,
                                          StripPart part, int anchorKey, int currentKey);

/** Undo-step name for a drag ("Drag low key", "Move zone", ...). */
const char* stripDragActionName (StripPart part, bool multiple);

/**
 * Live tooltip text for the zone being dragged, UTF-8, notes named with the C4 = 60 or
 * C3 = 60 convention, e.g. "Low C3  |  C3-G3" or "Root E3 (52)".
 */
std::string stripDragLabel (StripPart part, const Zone& zone, bool middleCIsC4, size_t zoneCount = 1);

} // namespace looper
