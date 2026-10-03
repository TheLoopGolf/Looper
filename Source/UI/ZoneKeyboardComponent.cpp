#include "ZoneKeyboardComponent.h"
#include "LooperLookAndFeel.h"
#include <algorithm>

namespace looper {

namespace {
constexpr float kDragThresholdPx = 3.0f;
}

ZoneKeyboardComponent::ZoneKeyboardComponent()
{
    setOpaque (false);
    setTitle ("Zone keyboard");
    setDescription ("Click a zone to select it (Cmd/Ctrl or Shift to add). Drag edges, root dot or body to edit; "
                    "Alt/Option-drag draws a key range. Click a root key to hear it.");
}

void ZoneKeyboardComponent::setZones (const std::vector<ZoneKeySpan>& zones)
{
    zones_ = zones;
    selection_.prune (zones_.size());
    if (zones_.empty())
        setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ZoneKeyboardComponent::clearZones()
{
    zones_.clear();
    selection_.clear();
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ZoneKeyboardComponent::setSelection (const ZoneSelection& selection)
{
    selection_ = selection;
    selection_.prune (zones_.size());
    repaint();
}

void ZoneKeyboardComponent::setSelectedZone (int index)
{
    ZoneSelection s;
    if (juce::isPositiveAndBelow (index, (int) zones_.size()))
        s.selectOnly ((size_t) index);
    setSelection (s);
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

juce::Rectangle<float> ZoneKeyboardComponent::keyArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f).reduced (6.0f, 8.0f);
}

KeyStripLayout ZoneKeyboardComponent::layout() const
{
    const auto a = keyArea();
    KeyStripLayout L;
    L.lowKey = lowKey_;
    L.highKey = highKey_;
    L.x = a.getX();
    L.y = a.getY();
    L.width = a.getWidth();
    L.height = a.getHeight();
    return L;
}

int ZoneKeyboardComponent::keyAt (juce::Point<float> p) const
{
    return layout().keyAt (p.x, p.y);
}

int ZoneKeyboardComponent::nextStackedZone (int key, int current) const
{
    std::vector<int> hits;
    for (int i = 0; i < (int) zones_.size(); ++i)
        if (key >= zones_[(size_t) i].low && key <= zones_[(size_t) i].high)
            hits.push_back (i);
    if (hits.size() < 2)
        return current;
    const auto it = std::find (hits.begin(), hits.end(), current);
    return (it == hits.end() || std::next (it) == hits.end()) ? hits.front() : *std::next (it);
}

void ZoneKeyboardComponent::stopAudition()
{
    if (auditioning_ >= 0 && onAudition)
        onAudition (auditioning_, false);
    auditioning_ = -1;
}

void ZoneKeyboardComponent::mouseDown (const juce::MouseEvent& e)
{
    pressPart_ = StripPart::None;
    pressZone_ = -1;
    selectOnlyOnUp_ = -1;
    cycleOnUp_ = false;
    dragging_ = false;
    tooltip_.clear();
    if (zones_.empty())
        return;

    const auto L = layout();
    const auto hit = hitTestStrip (L, zones_, selection_, e.position.x, e.position.y);
    pressKey_ = hit.key;

    if (e.mods.isAltDown())
    {
        // Alt/Option-drag: draw a key range for the selection (or the zone under the pointer)
        if (selection_.empty() && hit.zone >= 0 && onZoneClicked)
            onZoneClicked (hit.zone, ClickModifier::None);
        if (! selection_.empty())
        {
            pressPart_ = StripPart::DrawRange;
            anchorKey_ = L.nearestKey (e.position.x);
        }
        return;
    }

    if (hit.zone < 0)
        return;

    if (e.mods.isCommandDown() || e.mods.isShiftDown())
    {
        if (onZoneClicked)
            onZoneClicked (hit.zone, e.mods.isShiftDown() ? ClickModifier::Range : ClickModifier::Toggle);
        return; // modifier clicks only change the selection
    }

    if (! selection_.contains ((size_t) hit.zone))
    {
        if (onZoneClicked)
            onZoneClicked (hit.zone, ClickModifier::None);
    }
    else if (selection_.isMulti())
        selectOnlyOnUp_ = hit.zone;          // keep the group in case this becomes a drag
    else
        cycleOnUp_ = hit.part == StripPart::Body && hit.key != zones_[(size_t) hit.zone].root;

    pressPart_ = hit.part;
    pressZone_ = hit.zone;
    anchorKey_ = L.nearestKey (e.position.x);

    const auto& z = zones_[(size_t) hit.zone];
    if (onAudition && (hit.part == StripPart::Root || (hit.part == StripPart::Body && hit.key == z.root)))
    {
        auditioning_ = hit.zone;
        onAudition (hit.zone, true);
    }
}

void ZoneKeyboardComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (pressPart_ == StripPart::None)
        return;
    if (! dragging_)
    {
        if (e.getDistanceFromDragStart() < (int) kDragThresholdPx)
            return;
        stopAudition();
        selectOnlyOnUp_ = -1;
        cycleOnUp_ = false;
        if (! onDragStart || ! onDragStart (pressPart_, pressPart_ == StripPart::DrawRange ? -1 : pressZone_, anchorKey_))
        {
            pressPart_ = StripPart::None;
            return;
        }
        dragging_ = true;
        lastDragKey_ = -1;
    }
    const int key = layout().nearestKey (e.position.x);
    tooltipX_ = e.position.x;
    if (key != lastDragKey_)
    {
        lastDragKey_ = key;
        if (onDragMove)
            tooltip_ = onDragMove (key);
    }
    repaint();
}

void ZoneKeyboardComponent::mouseUp (const juce::MouseEvent& e)
{
    stopAudition();
    if (dragging_)
    {
        dragging_ = false;
        tooltip_.clear();
        if (onDragEnd)
            onDragEnd();
    }
    else if (selectOnlyOnUp_ >= 0)
    {
        if (onZoneClicked)
            onZoneClicked (selectOnlyOnUp_, ClickModifier::None);
    }
    else if (cycleOnUp_ && pressKey_ >= 0 && pressZone_ >= 0)
    {
        const int next = nextStackedZone (pressKey_, pressZone_);
        if (next != pressZone_ && onZoneClicked)
            onZoneClicked (next, ClickModifier::None);
    }
    pressPart_ = StripPart::None;
    selectOnlyOnUp_ = -1;
    cycleOnUp_ = false;
    updateCursor (&e, e.mods);
    repaint();
}

void ZoneKeyboardComponent::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (&e, e.mods);
}

void ZoneKeyboardComponent::mouseExit (const juce::MouseEvent&)
{
    if (! dragging_)
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ZoneKeyboardComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    if (isMouseOver() && ! dragging_)
        updateCursor (nullptr, mods);
}

void ZoneKeyboardComponent::updateCursor (const juce::MouseEvent* e, const juce::ModifierKeys& mods)
{
    if (zones_.empty())
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }
    if (mods.isAltDown() && ! selection_.empty())
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto pos = e != nullptr ? e->position : getMouseXYRelative().toFloat();
    const auto hit = hitTestStrip (layout(), zones_, selection_, pos.x, pos.y);
    switch (hit.part)
    {
        case StripPart::LowEdge:
        case StripPart::HighEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case StripPart::Body:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case StripPart::Root:     setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case StripPart::DrawRange:
        case StripPart::None:     setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void ZoneKeyboardComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Palette::bgSunken());
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Palette::border());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const auto L = layout();
    const auto area = keyArea();
    const float whiteW = L.whiteWidth();
    const float whiteH = L.height;
    const float blackH = L.blackHeight();
    const float blackW = L.blackWidth();

    // White keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (KeyStripLayout::isBlack (m))
            continue;
        auto key = juce::Rectangle<float> (L.keyLeft (m), area.getY(), whiteW - 1.0f, whiteH);
        g.setColour (zones_.empty() ? Palette::bgRaised().brighter (0.04f)
                                    : juce::Colour (0xffdce8de));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::border());
        g.drawRoundedRectangle (key, 2.0f, 0.8f);
    }

    auto spanRect = [&] (const ZoneKeySpan& z) -> juce::Rectangle<float> {
        const auto s = L.spanFor (z.low, z.high);
        if (! s.visible)
            return {};
        return { s.x0, area.getY(), s.x1 - s.x0, whiteH };
    };

    const bool anySelected = ! selection_.empty();
    // Zone overlays (behind black keys for readability)
    for (int zi = 0; zi < (int) zones_.size(); ++zi)
    {
        if (selection_.contains ((size_t) zi))
            continue;
        const auto overlay = spanRect (zones_[(size_t) zi]);
        if (overlay.isEmpty())
            continue;
        const auto colour = (zi % 2 == 0) ? Palette::fairway() : Palette::brass();
        g.setColour (colour.withAlpha (anySelected ? 0.16f : 0.28f));
        g.fillRoundedRectangle (overlay.reduced (0.5f), 2.0f);
        g.setColour (colour.withAlpha (anySelected ? 0.35f : 0.55f));
        g.drawRoundedRectangle (overlay.reduced (0.5f), 2.0f, 1.0f);
    }
    for (size_t zi : selection_.indices())
    {
        const auto overlay = spanRect (zones_[zi]);
        if (! overlay.isEmpty())
        {
            g.setColour (Palette::sand().withAlpha ((int) zi == selection_.primary() ? 0.30f : 0.22f));
            g.fillRoundedRectangle (overlay.reduced (0.5f), 2.0f);
        }
    }

    // Black keys
    for (int m = lowKey_; m <= highKey_; ++m)
    {
        if (! KeyStripLayout::isBlack (m))
            continue;
        auto key = juce::Rectangle<float> (L.keyLeft (m), area.getY(), blackW, blackH);
        g.setColour (zones_.empty() ? Palette::bg().darker (0.2f) : juce::Colour (0xff121a14));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (Palette::borderBright().withAlpha (0.5f));
        g.drawRoundedRectangle (key, 2.0f, 0.7f);
    }

    // Selected zone outlines + edge grips on top of everything
    for (size_t zi : selection_.indices())
    {
        const auto& z = zones_[zi];
        const auto overlay = spanRect (z);
        if (overlay.isEmpty())
            continue;
        const bool primary = (int) zi == selection_.primary();
        g.setColour (primary ? Palette::sand() : Palette::sand().withAlpha (0.75f));
        g.drawRoundedRectangle (overlay.reduced (1.0f), 2.5f, primary ? 2.0f : 1.2f);
        // Edge grips (flag-pin bars) where the edge is on screen
        const float gy = overlay.getY() + overlay.getHeight() * 0.30f;
        const float gh = overlay.getHeight() * 0.40f;
        g.setColour (primary ? Palette::brass() : Palette::brass().withAlpha (0.7f));
        if (z.low >= lowKey_)
            g.fillRoundedRectangle (overlay.getX() + 1.0f, gy, 3.0f, gh, 1.5f);
        if (z.high <= highKey_)
            g.fillRoundedRectangle (overlay.getRight() - 4.0f, gy, 3.0f, gh, 1.5f);
    }

    // Root markers ("ball on the tee"): dot near the bottom of each root key
    auto rootDot = [&] (int root, bool selected, bool primary) {
        if (root < lowKey_ || root > highKey_)
            return;
        const float cx = L.keyCentre (root);
        const float cy = L.rootMarkerY (root);
        const float r = primary ? 4.0f : (selected ? 3.4f : 2.6f);
        g.setColour (selected ? Palette::brass() : Palette::fairwayDim());
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        if (selected)
        {
            g.setColour (Palette::bg());
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    };
    for (int zi = 0; zi < (int) zones_.size(); ++zi)
        if (! selection_.contains ((size_t) zi))
            rootDot (zones_[(size_t) zi].root, false, false);
    for (size_t zi : selection_.indices())
        rootDot (zones_[zi].root, true, (int) zi == selection_.primary());

    if (zones_.empty())
    {
        g.setColour (Palette::muted().withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("zones appear after import", area, juce::Justification::centred, false);
    }

    // Live drag tooltip (note names follow the C4 / C3 = 60 setting)
    if (dragging_ && tooltip_.isNotEmpty())
    {
        const juce::Font f (juce::FontOptions (11.5f, juce::Font::bold));
        g.setFont (f);
        const float w = juce::GlyphArrangement::getStringWidth (f, tooltip_) + 16.0f;
        const float h = 20.0f;
        const float bx = juce::jlimit (bounds.getX() + 2.0f, bounds.getRight() - w - 2.0f, tooltipX_ - w * 0.5f);
        const auto bubble = juce::Rectangle<float> (bx, bounds.getY() + 2.0f, w, h);
        g.setColour (Palette::bg().withAlpha (0.92f));
        g.fillRoundedRectangle (bubble, 6.0f);
        g.setColour (Palette::brass());
        g.drawRoundedRectangle (bubble, 6.0f, 1.0f);
        g.setColour (Palette::text());
        g.drawText (tooltip_, bubble, juce::Justification::centred, false);
    }
}

} // namespace looper
