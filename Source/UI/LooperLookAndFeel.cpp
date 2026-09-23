#include "LooperLookAndFeel.h"

namespace looper {

LooperLookAndFeel::LooperLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::bg());
    setColour (juce::Label::textColourId, Palette::text());
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId, Palette::bgRaised());
    setColour (juce::TextButton::buttonOnColourId, Palette::fairwayDim());
    setColour (juce::TextButton::textColourOffId, Palette::text());
    setColour (juce::TextButton::textColourOnId, Palette::bg());

    setColour (juce::ComboBox::backgroundColourId, Palette::bgSunken());
    setColour (juce::ComboBox::outlineColourId, Palette::border());
    setColour (juce::ComboBox::textColourId, Palette::text());
    setColour (juce::ComboBox::arrowColourId, Palette::muted());
    setColour (juce::ComboBox::focusedOutlineColourId, Palette::fairway());

    setColour (juce::TextEditor::backgroundColourId, Palette::bgSunken());
    setColour (juce::TextEditor::outlineColourId, Palette::border());
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::fairway());
    setColour (juce::TextEditor::textColourId, Palette::text());
    setColour (juce::TextEditor::highlightColourId, Palette::fairwayDim());
    setColour (juce::TextEditor::highlightedTextColourId, Palette::text());

    setColour (juce::ListBox::backgroundColourId, Palette::bgSunken());
    setColour (juce::ListBox::outlineColourId, Palette::border());
    setColour (juce::ListBox::textColourId, Palette::text());

    setColour (juce::PopupMenu::backgroundColourId, Palette::bgRaised());
    setColour (juce::PopupMenu::textColourId, Palette::text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::fairwayDim());
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text());
    setColour (juce::PopupMenu::headerTextColourId, Palette::muted());

    setColour (juce::Slider::rotarySliderFillColourId, Palette::fairway());
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::border());
    setColour (juce::Slider::thumbColourId, Palette::brass());
    setColour (juce::Slider::textBoxTextColourId, Palette::text());
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId, Palette::borderBright());
    setColour (juce::ScrollBar::trackColourId, Palette::bgSunken());

    setColour (juce::TableHeaderComponent::backgroundColourId, Palette::bgRaised());
    setColour (juce::TableHeaderComponent::textColourId, Palette::muted());
    setColour (juce::TableHeaderComponent::outlineColourId, Palette::border());
}

void LooperLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackThickness = juce::jlimit (3.5f, 6.5f, radius * 0.18f);
    const float toAngle = rotaryStartAngle
                          + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - trackThickness * 0.5f,
                         radius - trackThickness * 0.5f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Palette::bgSunken());
    g.strokePath (track, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    g.setColour (Palette::border());
    g.strokePath (track, juce::PathStrokeType (1.2f));

    if (slider.isEnabled() && sliderPosProportional > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius - trackThickness * 0.5f,
                                radius - trackThickness * 0.5f, 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (valueArc, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    const float hubR = radius - trackThickness - 3.0f;
    if (hubR > 2.0f)
    {
        g.setColour (Palette::bgRaised());
        g.fillEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f);
        g.setColour (Palette::border());
        g.drawEllipse (centre.x - hubR, centre.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.0f);
    }

    const float tipR = radius - trackThickness * 0.5f;
    const juce::Point<float> tip (centre.x + tipR * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                  centre.y + tipR * std::sin (toAngle - juce::MathConstants<float>::halfPi));
    g.setColour (Palette::brass());
    g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
}

void LooperLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (10.0f, bounds.getHeight() * 0.5f);

    auto fill = backgroundColour;
    auto outline = Palette::border();

    const bool primary = fill.getPerceivedBrightness() > 0.35f
                         || fill.getGreen() > fill.getRed() + 20;
    const bool ghost = fill.isTransparent()
                       || (fill.getBrightness() < 0.12f && fill.getAlpha() < 0.95f);

    if (primary && ! ghost)
    {
        fill = Palette::fairway();
        outline = Palette::fairwayDim();
        if (shouldDrawButtonAsDown)
            fill = Palette::fairwayDim();
        else if (shouldDrawButtonAsHighlighted)
            fill = Palette::fairway().brighter (0.08f);
    }
    else
    {
        fill = Palette::bgRaised();
        outline = Palette::borderBright();
        if (shouldDrawButtonAsDown)
            fill = Palette::bgSunken();
        else if (shouldDrawButtonAsHighlighted)
        {
            fill = Palette::bgRaised().brighter (0.06f);
            outline = Palette::fairwayDim();
        }

        // Brass outline cue when button colour was set to brass-ish
        if (backgroundColour.getHue() > 0.08f && backgroundColour.getHue() < 0.2f
            && backgroundColour.getSaturation() > 0.3f)
            outline = Palette::brass();
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds, radius, shouldDrawButtonAsHighlighted ? 1.6f : 1.1f);
}

void LooperLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool, bool)
{
    auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);

    const auto bg = button.findColour (button.getToggleState() ? juce::TextButton::buttonOnColourId
                                                               : juce::TextButton::buttonColourId);
    const bool primary = bg.getPerceivedBrightness() > 0.35f
                         || bg.getGreen() > bg.getRed() + 20;
    g.setColour (primary ? Palette::bg()
                         : button.findColour (juce::TextButton::textColourOffId)
                               .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));

    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                juce::Justification::centred, false);
}

void LooperLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float radius = 6.0f;
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.hasKeyboardFocus (true) ? Palette::fairway()
                                             : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, radius, 1.1f);

    const float arrowX = (float) width - 14.0f;
    const float arrowY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowX - 4.0f, arrowY - 2.5f, arrowX + 4.0f, arrowY - 2.5f, arrowX, arrowY + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void LooperLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Palette::bgRaised());
    g.setColour (Palette::border());
    g.drawRect (0, 0, width, height, 1);
}

void LooperLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu, const juce::String& text,
                                           const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour (Palette::border());
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (1);
    if (isHighlighted && isActive)
    {
        g.setColour (Palette::fairwayDim());
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    g.setColour (textColourToUse != nullptr ? *textColourToUse
                                            : (isActive ? Palette::text() : Palette::muted()));
    g.setFont (juce::FontOptions (13.0f));
    auto textR = r.reduced (8, 0);
    if (isTicked)
        textR.removeFromLeft (14);
    g.drawFittedText (text, textR, juce::Justification::centredLeft, 1);

    if (isTicked)
    {
        g.setColour (Palette::fairway());
        g.fillEllipse ((float) r.getX() + 4.0f, (float) r.getCentreY() - 3.0f, 6.0f, 6.0f);
    }

    juce::ignoreUnused (hasSubMenu, shortcutKeyText, icon);
}

juce::Font LooperLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.48f)));
}

juce::Font LooperLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f));
}

juce::Font LooperLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

} // namespace looper
