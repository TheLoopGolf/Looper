#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../InstrumentMap/InstrumentMap.h"
#include <functional>
#include <vector>

namespace looper {

/**
 * Compact piano strip for zone visualization and selection (no MIDI input).
 * Click a key inside a zone to select that zone (repeated clicks cycle through stacked
 * zones on that key). Clicking a zone's root key (dot marker) also auditions it while
 * the mouse is held.
 */
class ZoneKeyboardComponent : public juce::Component
{
public:
    ZoneKeyboardComponent();
    ~ZoneKeyboardComponent() override = default;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** Spans indexed like InstrumentMap::zones. */
    void setZones (const std::vector<ZoneKeySpan>& zones);
    void clearZones();

    /** Highlighted zone (index into the spans), or -1. */
    void setSelectedZone (int index);
    int selectedZone() const noexcept { return selected_; }

    /** Visible MIDI range (inclusive). Default C2-C7 (36-96). */
    void setKeyRange (int lowMidi, int highMidi);
    /** Range that shows every root (at least C2-C7), snapped to whole octaves. */
    void fitKeyRangeToRoots();

    /** MIDI note under a local point inside the key area, or -1. */
    int keyAt (juce::Point<float> p) const;

    std::function<void (int zoneIndex)> onZoneClicked;
    std::function<void (int zoneIndex, bool down)> onAudition;

private:
    bool isBlackKey (int midi) const;
    float keyX (int midi, float whiteW) const;
    int countWhiteKeys() const;
    juce::Rectangle<float> keyArea() const;

    int lowKey_ = 36;
    int highKey_ = 96;
    std::vector<ZoneKeySpan> zones_;
    int selected_ = -1;
    int auditioning_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneKeyboardComponent)
};

} // namespace looper
