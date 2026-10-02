#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperLookAndFeel.h"

#include <functional>
#include <memory>

namespace looper {

/**
 * Compact segmented control (e.g. "Cycle | Random") in the fairway-night theme.
 * Optionally bound to a choice parameter (index == segment) via juce::ParameterAttachment,
 * so host automation, undo-less UI edits and preset recalls stay in sync both ways.
 * Mouse click, Left/Right keys and accessibility (radio buttons) select a segment.
 */
class SegmentedChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SegmentedChoice (juce::StringArray options);
    ~SegmentedChoice() override;

    /** Bind to a choice/int parameter whose values are 0..numSegments-1. */
    void attachToParameter (juce::RangedAudioParameter& param);

    int getSelectedIndex() const noexcept { return selected_; }
    void setSelectedIndex (int index, juce::NotificationType notify);
    int getNumSegments() const noexcept { return options_.size(); }
    const juce::StringArray& getOptions() const noexcept { return options_; }

    /** De-emphasised look (still clickable), e.g. when the patch has no RR alternates. */
    void setSubdued (bool subdued);

    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int segmentAt (juce::Point<int> p) const;
    juce::Rectangle<float> segmentBounds (int index) const;
    void userSelect (int index);

    juce::StringArray options_;
    int selected_ = 0;
    int hover_ = -1;
    bool subdued_ = false;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedChoice)
};

/** Button that draws a vector gear (no font glyph needed, so it renders identically everywhere). */
class GearButton : public juce::Button
{
public:
    GearButton();
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    /** Gear outline + hub hole, centred in `area` (even-odd fill). */
    static juce::Path makeGearPath (juce::Rectangle<float> area, int teeth = 8);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearButton)
};

} // namespace looper
