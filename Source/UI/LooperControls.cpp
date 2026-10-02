#include "LooperControls.h"

namespace looper {

//==============================================================================
SegmentedChoice::SegmentedChoice (juce::StringArray options) : options_ (std::move (options))
{
    jassert (options_.size() >= 2);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

SegmentedChoice::~SegmentedChoice() = default;

void SegmentedChoice::attachToParameter (juce::RangedAudioParameter& param)
{
    attachment_ = std::make_unique<juce::ParameterAttachment> (
        param,
        [this] (float denormalised) {
            setSelectedIndex (juce::roundToInt (denormalised), juce::dontSendNotification);
        },
        nullptr);
    attachment_->sendInitialUpdate();
}

void SegmentedChoice::setSelectedIndex (int index, juce::NotificationType notify)
{
    index = juce::jlimit (0, options_.size() - 1, index);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    if (notify != juce::dontSendNotification && onChange)
        onChange (selected_);
}

void SegmentedChoice::setSubdued (bool subdued)
{
    if (subdued_ != subdued)
    {
        subdued_ = subdued;
        repaint();
    }
}

void SegmentedChoice::userSelect (int index)
{
    index = juce::jlimit (0, options_.size() - 1, index);
    const int before = selected_;
    if (attachment_ != nullptr)
        attachment_->setValueAsCompleteGesture ((float) index); // may also update selected_
    setSelectedIndex (index, juce::dontSendNotification);
    if (before != selected_ && onChange)
        onChange (selected_);
}

juce::Rectangle<float> SegmentedChoice::segmentBounds (int index) const
{
    auto track = getLocalBounds().toFloat().reduced (1.0f);
    const float w = track.getWidth() / (float) options_.size();
    return { track.getX() + w * (float) index, track.getY(), w, track.getHeight() };
}

int SegmentedChoice::segmentAt (juce::Point<int> p) const
{
    for (int i = 0; i < options_.size(); ++i)
        if (segmentBounds (i).contains (p.toFloat()))
            return i;
    return -1;
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    const auto track = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (8.0f, track.getHeight() * 0.5f);
    const float alpha = isEnabled() ? (subdued_ ? 0.6f : 1.0f) : 0.4f;

    g.setColour (Palette::bgSunken());
    g.fillRoundedRectangle (track, radius);
    g.setColour ((hasKeyboardFocus (false) ? Palette::fairwayDim() : Palette::borderBright()).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (track, radius, 1.0f);

    // Selected "pill": deep fairway with a fine brass hairline, like a club badge.
    const auto pill = segmentBounds (selected_).reduced (2.0f);
    const float pillR = juce::jmax (2.0f, radius - 2.0f);
    g.setColour (Palette::fairwayDim().withAlpha (0.55f * alpha));
    g.fillRoundedRectangle (pill, pillR);
    g.setColour (Palette::brass().withAlpha (0.55f * alpha));
    g.drawRoundedRectangle (pill, pillR, 0.8f);

    g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.5f, track.getHeight() * 0.58f))));
    for (int i = 0; i < options_.size(); ++i)
    {
        const bool sel = (i == selected_);
        auto colour = sel ? Palette::text() : (i == hover_ ? Palette::text().withAlpha (0.85f) : Palette::muted());
        g.setColour (colour.withMultipliedAlpha (alpha));
        g.drawText (options_[i], segmentBounds (i).toNearestInt(), juce::Justification::centred, false);
    }
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    const int seg = segmentAt (e.getPosition());
    if (seg >= 0)
        userSelect (seg);
}

void SegmentedChoice::mouseMove (const juce::MouseEvent& e)
{
    const int seg = segmentAt (e.getPosition());
    if (seg != hover_)
    {
        hover_ = seg;
        repaint();
    }
}

void SegmentedChoice::mouseExit (const juce::MouseEvent&)
{
    hover_ = -1;
    repaint();
}

bool SegmentedChoice::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::leftKey))  { userSelect (selected_ - 1); return true; }
    if (key.isKeyCode (juce::KeyPress::rightKey)) { userSelect (selected_ + 1); return true; }
    if (key.isKeyCode (juce::KeyPress::spaceKey) || key.isKeyCode (juce::KeyPress::returnKey))
    {
        userSelect ((selected_ + 1) % options_.size());
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> SegmentedChoice::createAccessibilityHandler()
{
    struct ValueIface : public juce::AccessibilityTextValueInterface
    {
        explicit ValueIface (SegmentedChoice& o) : owner (o) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override { return owner.getOptions()[owner.getSelectedIndex()]; }
        void setValueAsString (const juce::String& v) override
        {
            const int idx = owner.getOptions().indexOf (v, true);
            if (idx >= 0)
                owner.userSelect (idx);
        }
        SegmentedChoice& owner;
    };

    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::comboBox,
        juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press,
                                                [this] { userSelect ((selected_ + 1) % options_.size()); }),
        juce::AccessibilityHandler::Interfaces { std::make_unique<ValueIface> (*this) });
}

//==============================================================================
GearButton::GearButton() : juce::Button ("Settings")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Path GearButton::makeGearPath (juce::Rectangle<float> area, int teeth)
{
    const auto c = area.getCentre();
    const float rOuter = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const float rRoot = rOuter * 0.76f;
    const float rHole = rOuter * 0.34f;
    const float step = juce::MathConstants<float>::twoPi / (float) teeth;
    const float toothHalf = step * 0.22f; // flat tooth top
    const float flank = step * 0.08f;     // slight taper

    juce::Path p;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = (float) i * step - juce::MathConstants<float>::halfPi;
        auto pt = [&] (float r, float ang) {
            return juce::Point<float> (c.x + r * std::cos (ang), c.y + r * std::sin (ang));
        };
        const auto p0 = pt (rRoot, a - toothHalf - flank);
        if (i == 0) p.startNewSubPath (p0); else p.lineTo (p0);
        p.lineTo (pt (rOuter, a - toothHalf));
        p.lineTo (pt (rOuter, a + toothHalf));
        p.lineTo (pt (rRoot, a + toothHalf + flank));
        // root arc to the next tooth
        p.addCentredArc (c.x, c.y, rRoot, rRoot, 0.0f,
                         a + toothHalf + flank + juce::MathConstants<float>::halfPi,
                         a + step - toothHalf - flank + juce::MathConstants<float>::halfPi, false);
    }
    p.closeSubPath();
    p.addEllipse (c.x - rHole, c.y - rHole, rHole * 2.0f, rHole * 2.0f);
    p.setUsingNonZeroWinding (false);
    return p;
}

void GearButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                                           highlighted, down);
    const float s = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.56f;
    const auto iconArea = getLocalBounds().toFloat().withSizeKeepingCentre (s, s);
    g.setColour ((highlighted ? Palette::fairway() : Palette::text()).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
    g.fillPath (makeGearPath (iconArea));
}

} // namespace looper
