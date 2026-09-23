#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../InstrumentMap/InstrumentMap.h"
#include <vector>

namespace looper {

/** Compact piano strip for zone visualization (display-only; no MIDI input). */
class ZoneKeyboardComponent : public juce::Component
{
public:
    ZoneKeyboardComponent();
    ~ZoneKeyboardComponent() override = default;

    void paint (juce::Graphics& g) override;

    void setZones (const std::vector<ZoneKeySpan>& zones);
    void clearZones();

    /** Visible MIDI range (inclusive). Default C2–C7 (36–96). */
    void setKeyRange (int lowMidi, int highMidi);

private:
    bool isBlackKey (int midi) const;
    float keyX (int midi, float whiteW) const;
    int countWhiteKeys() const;

    int lowKey_ = 36;
    int highKey_ = 96;
    std::vector<ZoneKeySpan> zones_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneKeyboardComponent)
};

} // namespace looper
