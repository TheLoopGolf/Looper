#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../InstrumentMap/InstrumentMap.h"
#include "../ZoneEdit/ZoneStrip.h"
#include <functional>
#include <vector>

namespace looper {

/**
 * Piano strip for zone visualization, selection and direct editing (no MIDI input).
 *
 *  - Click a zone to select it; Cmd/Ctrl-click toggles it in a multi-selection, Shift-click
 *    selects a range (keyboard order). Repeated plain clicks cycle through stacked zones.
 *  - Drag a zone's left / right edge to change its low / high key, its root marker (dot) to
 *    change the root, or its body to move the whole zone (width kept). With several zones
 *    selected the same drag moves every selected zone by the same number of keys.
 *  - Alt/Option-drag draws a key range for the selected zone(s).
 *  - Clicking a zone's root key auditions it while the mouse is held.
 * Every drag snaps to keys, shows a live tooltip, and becomes one undo step (the owner turns
 * onDragMove keys into edits; the maths lives in ZoneEdit/ZoneStrip, unit-tested).
 */
class ZoneKeyboardComponent : public juce::Component
{
public:
    ZoneKeyboardComponent();
    ~ZoneKeyboardComponent() override = default;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void modifierKeysChanged (const juce::ModifierKeys& mods) override;

    /** Spans indexed like InstrumentMap::zones. */
    void setZones (const std::vector<ZoneKeySpan>& zones);
    void clearZones();

    /** Highlighted zones (indices into the spans). */
    void setSelection (const ZoneSelection& selection);
    const ZoneSelection& selection() const noexcept { return selection_; }
    /** Single-zone convenience (index or -1). */
    void setSelectedZone (int index);
    int selectedZone() const noexcept { return selection_.primary(); }

    /** Visible MIDI range (inclusive). Default C2-C7 (36-96). */
    void setKeyRange (int lowMidi, int highMidi);
    /** Range that shows every root (at least C2-C7), snapped to whole octaves. */
    void fitKeyRangeToRoots();

    /** MIDI note under a local point inside the key area, or -1. */
    int keyAt (juce::Point<float> p) const;
    /** Current key geometry (local coordinates). */
    KeyStripLayout layout() const;

    bool isDragging() const noexcept { return dragging_; }

    /** Selection click on a zone (plain / Cmd-Ctrl toggle / Shift range). */
    std::function<void (int zoneIndex, ClickModifier mod)> onZoneClicked;
    std::function<void (int zoneIndex, bool down)> onAudition;
    /** A drag started on `zoneIndex` (-1 for DrawRange). Return false to ignore the drag. */
    std::function<bool (StripPart part, int zoneIndex, int anchorKey)> onDragStart;
    /** Pointer snapped to `key`; returns the tooltip text to show. */
    std::function<juce::String (int key)> onDragMove;
    std::function<void()> onDragEnd;

private:
    juce::Rectangle<float> keyArea() const;
    void stopAudition();
    void updateCursor (const juce::MouseEvent* e, const juce::ModifierKeys& mods);
    int nextStackedZone (int key, int current) const;

    int lowKey_ = 36;
    int highKey_ = 96;
    std::vector<ZoneKeySpan> zones_;
    ZoneSelection selection_;
    int auditioning_ = -1;

    // Press / drag state
    StripPart pressPart_ = StripPart::None;
    int pressZone_ = -1;
    int pressKey_ = -1;
    int anchorKey_ = -1;
    int lastDragKey_ = -1;
    int selectOnlyOnUp_ = -1;   // plain click on a member of a multi-selection
    bool cycleOnUp_ = false;    // plain click on the sole selected zone's body
    bool dragging_ = false;
    juce::String tooltip_;
    float tooltipX_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneKeyboardComponent)
};

} // namespace looper
