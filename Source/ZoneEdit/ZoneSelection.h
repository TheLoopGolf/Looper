#pragma once

#include "ZoneEditor.h"

#include <cstddef>
#include <string>
#include <vector>

namespace looper {

/**
 * Multi-zone selection (zones list + keyboard strip) and the edits that apply to it.
 * JUCE-free so the selection rules and the relative / absolute edit maths are unit-tested.
 *
 * Indices are positions in InstrumentMap::zones. The primary zone is the one most recently
 * clicked: Audition plays it, drags on the strip measure from it, and the editor shows its
 * value for fields where the selection is mixed.
 */
class ZoneSelection
{
public:
    /** Sorted ascending, no duplicates. */
    const std::vector<size_t>& indices() const noexcept { return indices_; }
    int primary() const noexcept { return primary_; }
    size_t size() const noexcept { return indices_.size(); }
    bool empty() const noexcept { return indices_.empty(); }
    bool isMulti() const noexcept { return indices_.size() > 1; }
    bool contains (size_t index) const noexcept;

    void clear() noexcept;
    /** Plain click: just this zone (also the new range anchor). */
    void selectOnly (size_t index);
    /** Cmd/Ctrl-click: add or remove one zone. Removing the primary promotes the nearest left. */
    void toggle (size_t index);
    /**
     * Shift-click: every zone between the anchor and `index` in display order (the zones list /
     * keyboard order). The anchor stays where it was so repeated Shift-clicks re-span from it.
     * Falls back to selectOnly when there is no anchor or `index` is not in `order`.
     */
    void selectRange (const std::vector<size_t>& order, size_t index);
    /** Cmd/Ctrl+A: zones 0..count-1 (primary kept when still valid, else the first). */
    void selectAll (size_t count);
    /** Replace the selection (e.g. from the list box). Primary must be one of them or -1. */
    void set (std::vector<size_t> indices, int primary);
    /** Make an already-selected zone the primary (drag start on a multi-selection). */
    void setPrimary (size_t index);
    /** Drop indices >= count (map shrank / was replaced). */
    void prune (size_t count);

private:
    void normalise();

    std::vector<size_t> indices_;
    int primary_ = -1;
    int anchor_ = -1;
};

enum class ClickModifier
{
    None,    // select only
    Toggle,  // Cmd (macOS) / Ctrl (Windows, Linux)
    Range    // Shift
};

/** Selection change for a click on zone `index` (strip or list) with modifiers. */
void applySelectionClick (ZoneSelection& selection, size_t index, ClickModifier mod,
                          const std::vector<size_t>& displayOrder);

// --- Mixed values ---------------------------------------------------------------------------

struct FieldSummary
{
    size_t count = 0;      // selected zones that exist in the map
    bool mixed = false;    // not every selected zone has the same value
    double first = 0.0;    // value of the primary (or first) zone
    double minValue = 0.0;
    double maxValue = 0.0;
};

/** Values of `field` across the selected zones (primary first when given). */
FieldSummary summarizeField (const InstrumentMap& map, const std::vector<size_t>& indices,
                             ZoneField field, int primary = -1);

// --- Multi-zone edits -----------------------------------------------------------------------

/**
 * True when a multi-selection edit of this field is an offset from each zone's own value
 * (root / key edges / fine tune / gain). Velocity edges and RR fields are set absolutely.
 */
bool fieldEditsRelative (ZoneField field);

/** Every selected zone with `field` moved by `delta` from its value in `origin` (each clamped). */
std::vector<ZoneChange> offsetField (const InstrumentMap& origin, const std::vector<size_t>& indices,
                                     ZoneField field, double delta);

/** Every selected zone with `field` set to `value` (clamped; ranges never invert). */
std::vector<ZoneChange> setFieldAll (const InstrumentMap& origin, const std::vector<size_t>& indices,
                                     ZoneField field, double value);

/**
 * Largest shift in [-|semis|, +|semis|] (same sign as `semis`) that keeps every selected key
 * range inside 0..127, so the group moves together and keeps its widths.
 */
int clampKeyShift (const InstrumentMap& origin, const std::vector<size_t>& indices, int semis);

/** Move key range and root of every selected zone by the same (clamped) number of semitones. */
std::vector<ZoneChange> shiftKeys (const InstrumentMap& origin, const std::vector<size_t>& indices, int semis);

/** Apply `fn(zone)` to each selected zone (Use detected pitch / Reset to auto on many zones). */
template <typename Fn>
std::vector<ZoneChange> mapSelected (const InstrumentMap& origin, const std::vector<size_t>& indices, Fn&& fn)
{
    std::vector<ZoneChange> out;
    for (size_t i : indices)
        if (i < origin.zones.size())
            out.push_back ({ i, fn (origin.zones[i]) });
    return out;
}

/**
 * How text typed into a field applies to a multi-selection: "+5" / "-3" offset every zone
 * (relative), anything else sets every zone (absolute). "=-6" forces an absolute negative.
 * `body` is the text to parse as a number / note name (sign kept for relative input).
 */
struct MultiEditText
{
    bool relative = false;
    std::string body;
};
MultiEditText classifyMultiEditText (const std::string& text);

} // namespace looper
