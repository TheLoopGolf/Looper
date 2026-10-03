#include "ZoneKeyboardComponent.h"
#include "LooperLookAndFeel.h"
#include <algorithm>

namespace looper {

ZoneKeyboardComponent::ZoneKeyboardComponent()
{
    setOpaque (false);
    setTitle ("Zone keyboard");
    setDescription ("Click a zone to edit it; click its root key to audition.");
}

void ZoneKeyboardComponent::setZones (const std::vector<ZoneKeySpan>& zones)
{
    zones_ = zones;
    if (selected_ >= (int) zones_.size())
        selected_ = -1;
    setMouseCursor (zones_.empty() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void ZoneKeyboardComponent::clearZones()
{
    zones_.clear();
    selected_ = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ZoneKeyboardComponent::setSelectedZone (int index)
{
    const int next = juce::isPositiveAndBelow (index, (int) zones_.size()) ? index : -1;
    if (next == selected_)
        return;
    selected_ = next;
    repaint();
}

void ZoneKeyboardComponent::setKeyRange (int lowMidi, int highMidi)
{
    lowKey_ = juce::jlimit (0, 127, lowMidi);
    highKey_ = juce::jlimit (lowKey_, 127, highMidi);
    repaint();
}

void ZoneKeyboardComponent::fitKeyRangeToRoots()
{
    int lo = 36, hi = 96;
    for (const auto& z : zones_)
    {
        if (z.root < 0)
            continue;
        lo = juce::jmin (lo, z.root - 5);
        hi = juce::jmax (hi, z.root + 5);
    }
    lo = juce::jmax (0, lo - (((lo % 12) + 12) % 12));       // down to a C
    hi = juce::jmin (127, hi + (12 - ((hi % 12) + 12) % 12)); // up to the next C
    setKeyRange (lo, hi);
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

juce::Rectangle<float> ZoneKeyboardComponent::keyArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f).reduced (6.0f, 8.0f);
}

int ZoneKeyboardComponent::keyAt (juce::Point<float> p) const
{
    const auto area = keyArea();
    if (! area.contains (p))
        return -1;
    const float whiteW = area.getWidth() / (float) countWhiteKeys();
    const float blackW = whiteW * 0.62f;
    const float blackH = area.getHeight() * 0.62f;
    if (p.y < area.getY() + blackH)
    {
        for (int m = lowKey_; m <= highKey_; ++m)
        {
            if (! isBlackKey (m))
                continue;
            const float x = area.getX() + keyX (m, whiteW);
            if (p.x >= x && p.x < x + blackW)
                return m;
        }
    }
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (isBlackKey (m))
            continue;
        const float x = area.getX() + keyX (m, whiteW);
        if (p.x >= x && p.x < x + whiteW)
            return m;
    }
    return -1;
}

void ZoneKeyboardComponent::mouseDown (const juce::MouseEvent& e)
{
    const int key = keyAt (e.position);
    if (key < 0 || zones_.empty())
        return;

    // Zones covering this key, in map order
    std::vector<int> hits;
    for (int i = 0; i < (int) zones_.size(); ++i)
        if (key >= zones_[(size_t) i].low && key <= zones_[(size_t) i].high)
            hits.push_back (i);
    if (hits.empty())
        return;

    // A zone whose root is this key wins (audition target); else cycle stacked zones.
    int pick = -1;
    if (selected_ >= 0 && zones_[(size_t) selected_].root == key
        && std::find (hits.begin(), hits.end(), selected_) != hits.end())
        pick = selected_;
    if (pick < 0)
        for (int i : hits)
            if (zones_[(size_t) i].root == key) { pick = i; break; }
    if (pick < 0)
    {
        const auto it = std::find (hits.begin(), hits.end(), selected_);
        pick = (it == hits.end() || std::next (it) == hits.end()) ? hits.front() : *std::next (it);
    }

    setSelectedZone (pick);
    if (onZoneClicked)
        onZoneClicked (pick);
    if (zones_[(size_t) pick].root == key && onAudition)
    {
        auditioning_ = pick;
        onAudition (pick, true);
    }
}

void ZoneKeyboardComponent::mouseUp (const juce::MouseEvent&)
{
    if (auditioning_ >= 0 && onAudition)
        onAudition (auditioning_, false);
    auditioning_ = -1;
}

void ZoneKeyboardComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Palette::bgSunken());
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const auto area = keyArea();
    const int whiteCount = countWhiteKeys();
    const float whiteW = area.getWidth() / (float) whiteCount;
    const float whiteH = area.getHeight();
    const float blackH = whiteH * 0.62f;
    const float blackW = whiteW * 0.62f;

    // White keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (isBlackKey (m))
            continue;
        const float x = area.getX() + keyX (m, whiteW);
        auto key = juce::Rectangle<float> (x, area.getY(), whiteW - 1.0f, whiteH);
        g.setColour (zones_.empty() ? Palette::bgRaised().brighter (0.04f)
                                    : juce::Colour (0xffdce8de));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::border());
        g.drawRoundedRectangle (key, 2.0f, 0.8f);
    }

    auto spanRect = [&] (const ZoneKeySpan& z) -> juce::Rectangle<float> {
        const int lo = juce::jmax (lowKey_, z.low);
        const int hi = juce::jmin (highKey_, z.high);
        if (lo > hi)
            return {};
        float x0 = area.getWidth();
        float x1 = 0.0f;
        for (int m = lo; m <= hi; ++m)
        {
            const float x = keyX (m, whiteW);
            const float w = isBlackKey (m) ? blackW : whiteW;
            x0 = juce::jmin (x0, x);
            x1 = juce::jmax (x1, x + w);
        }
        if (x1 <= x0)
            return {};
        return { area.getX() + x0, area.getY(), x1 - x0, whiteH };
    };

    // Zone overlays (behind black keys for readability)
    for (int zi = 0; zi < (int) zones_.size(); ++zi)
    {
        if (zi == selected_)
            continue;
        const auto overlay = spanRect (zones_[(size_t) zi]);
        if (overlay.isEmpty())
            continue;
        const auto colour = (zi % 2 == 0) ? Palette::fairway() : Palette::brass();
        g.setColour (colour.withAlpha (selected_ >= 0 ? 0.16f : 0.28f));
        g.fillRoundedRectangle (overlay.reduced (0.5f), 2.0f);
        g.setColour (colour.withAlpha (selected_ >= 0 ? 0.35f : 0.55f));
        g.drawRoundedRectangle (overlay.reduced (0.5f), 2.0f, 1.0f);
    }
    if (selected_ >= 0)
    {
        const auto overlay = spanRect (zones_[(size_t) selected_]);
        if (! overlay.isEmpty())
        {
            g.setColour (Palette::sand().withAlpha (0.30f));
            g.fillRoundedRectangle (overlay.reduced (0.5f), 2.0f);
        }
    }

    // Black keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (! isBlackKey (m))
            continue;
        const float x = area.getX() + keyX (m, whiteW);
        auto key = juce::Rectangle<float> (x, area.getY(), blackW, blackH);
        g.setColour (zones_.empty() ? Palette::bg().darker (0.2f) : juce::Colour (0xff121a14));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::borderBright().withAlpha (0.5f));
        g.drawRoundedRectangle (key, 2.0f, 0.7f);
    }

    // Selected zone outline on top of everything
    if (selected_ >= 0)
    {
        const auto overlay = spanRect (zones_[(size_t) selected_]);
        if (! overlay.isEmpty())
        {
            g.setColour (Palette::sand());
            g.drawRoundedRectangle (overlay.reduced (1.0f), 2.5f, 2.0f);
        }
    }

    // Root markers ("ball on the tee"): dot near the bottom of each root key
    auto rootDot = [&] (int root, bool selected) {
        if (root < lowKey_ || root > highKey_)
            return;
        const bool black = isBlackKey (root);
        const float w = black ? blackW : whiteW;
        const float cx = area.getX() + keyX (root, whiteW) + (w - (black ? 0.0f : 1.0f)) * 0.5f;
        const float cy = area.getY() + (black ? blackH : whiteH) - 7.0f;
        const float r = selected ? 4.0f : 2.6f;
        g.setColour (selected ? Palette::brass() : Palette::fairwayDim());
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        if (selected)
        {
            g.setColour (Palette::bg());
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    };
    for (int zi = 0; zi < (int) zones_.size(); ++zi)
        if (zi != selected_)
            rootDot (zones_[(size_t) zi].root, false);
    if (selected_ >= 0)
        rootDot (zones_[(size_t) selected_].root, true);

    if (zones_.empty())
    {
        g.setColour (Palette::muted().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("zones appear after import", area, juce::Justification::centred, false);
    }
}

} // namespace looper
