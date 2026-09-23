#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace looper {

/** Fairway-night palette — Loop Audio Lab / The Loop Golf. */
namespace Palette
{
inline juce::Colour bg()          { return juce::Colour (0xff0a1610); }
inline juce::Colour bgRaised()    { return juce::Colour (0xff12261c); }
inline juce::Colour bgSunken()    { return juce::Colour (0xff08120d); }
inline juce::Colour border()      { return juce::Colour (0xff1e3a2c); }
inline juce::Colour borderBright(){ return juce::Colour (0xff2f5a42); }
inline juce::Colour fairway()     { return juce::Colour (0xff3ddc87); }
inline juce::Colour fairwayDim()  { return juce::Colour (0xff1f8a52); }
inline juce::Colour brass()       { return juce::Colour (0xffc9a227); }
inline juce::Colour sand()        { return juce::Colour (0xffe8c547); }
inline juce::Colour text()        { return juce::Colour (0xffe8ede8); }
inline juce::Colour muted()       { return juce::Colour (0xff8a9a8e); }
inline juce::Colour danger()      { return juce::Colour (0xffe85d4c); }
} // namespace Palette

/** Draws a simple flagstick / pin (~16px) for the wordmark. */
inline void drawFlagstick (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float cx = bounds.getCentreX();
    const float top = bounds.getY() + 1.0f;
    const float bottom = bounds.getBottom() - 1.0f;
    const float poleH = bottom - top;

    g.setColour (Palette::brass());
    g.fillRect (cx - 0.75f, top + 2.0f, 1.5f, poleH - 2.0f);

    juce::Path flag;
    flag.startNewSubPath (cx + 0.5f, top + 2.0f);
    flag.lineTo (cx + bounds.getWidth() * 0.55f, top + poleH * 0.28f);
    flag.lineTo (cx + 0.5f, top + poleH * 0.42f);
    flag.closeSubPath();
    g.setColour (Palette::fairway());
    g.fillPath (flag);

    g.setColour (Palette::text().withAlpha (0.9f));
    g.fillEllipse (cx - 2.2f, bottom - 4.5f, 4.4f, 4.0f);
}

class LooperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LooperLookAndFeel();
    ~LooperLookAndFeel() override = default;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperLookAndFeel)
};

} // namespace looper
