#include "ZoneKeyboardComponent.h"
#include "LooperLookAndFeel.h"

namespace looper {

ZoneKeyboardComponent::ZoneKeyboardComponent()
{
    setOpaque (false);
}

void ZoneKeyboardComponent::setZones (const std::vector<ZoneKeySpan>& zones)
{
    zones_ = zones;
    repaint();
}

void ZoneKeyboardComponent::clearZones()
{
    zones_.clear();
    repaint();
}

void ZoneKeyboardComponent::setKeyRange (int lowMidi, int highMidi)
{
    lowKey_ = juce::jlimit (0, 127, lowMidi);
    highKey_ = juce::jlimit (lowKey_, 127, highMidi);
    repaint();
}

bool ZoneKeyboardComponent::isBlackKey (int midi) const
{
    switch (midi % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

int ZoneKeyboardComponent::countWhiteKeys() const
{
    int n = 0;
    for (int m = lowKey_; m <= highKey_; ++m)
        if (! isBlackKey (m))
            ++n;
    return juce::jmax (1, n);
}

float ZoneKeyboardComponent::keyX (int midi, float whiteW) const
{
    int whiteIndex = 0;
    for (int m = lowKey_; m < midi; ++m)
        if (! isBlackKey (m))
            ++whiteIndex;
    if (isBlackKey (midi))
        return (float) whiteIndex * whiteW - whiteW * 0.35f;
    return (float) whiteIndex * whiteW;
}

void ZoneKeyboardComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Palette::bgSunken());
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    auto keyArea = bounds.reduced (6.0f, 8.0f);
    const int whiteCount = countWhiteKeys();
    const float whiteW = keyArea.getWidth() / (float) whiteCount;
    const float whiteH = keyArea.getHeight();
    const float blackH = whiteH * 0.62f;
    const float blackW = whiteW * 0.62f;

    // White keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (isBlackKey (m))
            continue;
        const float x = keyArea.getX() + keyX (m, whiteW);
        auto key = juce::Rectangle<float> (x, keyArea.getY(), whiteW - 1.0f, whiteH);
        g.setColour (zones_.empty() ? Palette::bgRaised().brighter (0.04f)
                                    : juce::Colour (0xffdce8de));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::border());
        g.drawRoundedRectangle (key, 2.0f, 0.8f);
    }

    // Zone overlays (behind black keys for readability)
    if (! zones_.empty())
    {
        int zi = 0;
        for (const auto& z : zones_)
        {
            const int lo = juce::jmax (lowKey_, z.low);
            const int hi = juce::jmin (highKey_, z.high);
            if (lo > hi)
                continue;

            float x0 = keyArea.getWidth();
            float x1 = 0.0f;
            for (int m = lo; m <= hi; ++m)
            {
                if (isBlackKey (m))
                    continue;
                const float x = keyX (m, whiteW);
                x0 = juce::jmin (x0, x);
                x1 = juce::jmax (x1, x + whiteW);
            }
            if (x1 <= x0)
                continue;

            auto overlay = juce::Rectangle<float> (keyArea.getX() + x0, keyArea.getY(),
                                                   x1 - x0, whiteH);
            const auto colour = (zi % 2 == 0) ? Palette::fairway() : Palette::brass();
            g.setColour (colour.withAlpha (0.28f));
            g.fillRoundedRectangle (overlay.reduced (0.5f), 2.0f);
            g.setColour (colour.withAlpha (0.55f));
            g.drawRoundedRectangle (overlay.reduced (0.5f), 2.0f, 1.0f);
            ++zi;
        }
    }

    // Black keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (! isBlackKey (m))
            continue;
        const float x = keyArea.getX() + keyX (m, whiteW);
        auto key = juce::Rectangle<float> (x, keyArea.getY(), blackW, blackH);
        g.setColour (zones_.empty() ? Palette::bg().darker (0.2f) : juce::Colour (0xff121a14));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::borderBright().withAlpha (0.5f));
        g.drawRoundedRectangle (key, 2.0f, 0.7f);
    }

    if (zones_.empty())
    {
        g.setColour (Palette::muted().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("zones appear after import", keyArea, juce::Justification::centred, false);
    }
}

} // namespace looper
