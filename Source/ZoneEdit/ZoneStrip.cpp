#include "ZoneStrip.h"

#include "../AutoMapper/FilenameTokens.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace looper {

bool KeyStripLayout::isBlack (int midi) noexcept
{
    switch (((midi % 12) + 12) % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

int KeyStripLayout::whiteCount() const noexcept
{
    int n = 0;
    for (int m = lowKey; m <= highKey; ++m)
        if (! isBlack (m))
            ++n;
    return std::max (1, n);
}

float KeyStripLayout::whiteWidth() const noexcept
{
    return width / static_cast<float> (whiteCount());
}

float KeyStripLayout::keyLeft (int midi) const noexcept
{
    // White keys left of `midi` (negative when midi is below the visible range)
    int whiteIndex = 0;
    if (midi >= lowKey)
    {
        for (int m = lowKey; m < midi; ++m)
            if (! isBlack (m))
                ++whiteIndex;
    }
    else
    {
        for (int m = midi; m < lowKey; ++m)
            if (! isBlack (m))
                --whiteIndex;
    }
    const float w = whiteWidth();
    const float left = x + static_cast<float> (whiteIndex) * w;
    return isBlack (midi) ? left - w * 0.35f : left;
}

float KeyStripLayout::keyWidth (int midi) const noexcept
{
    return isBlack (midi) ? blackWidth() : whiteWidth();
}

float KeyStripLayout::keyCentre (int midi) const noexcept
{
    const bool black = isBlack (midi);
    return keyLeft (midi) + (keyWidth (midi) - (black ? 0.0f : 1.0f)) * 0.5f;
}

float KeyStripLayout::rootMarkerY (int midi) const noexcept
{
    return y + (isBlack (midi) ? blackHeight() : height) - 7.0f;
}

int KeyStripLayout::keyAt (float px, float py) const noexcept
{
    if (px < x || px >= x + width || py < y || py >= y + height)
        return -1;
    if (py < y + blackHeight())
        for (int m = lowKey; m <= highKey; ++m)
            if (isBlack (m) && px >= keyLeft (m) && px < keyLeft (m) + blackWidth())
                return m;
    const float w = whiteWidth();
    for (int m = lowKey; m <= highKey; ++m)
        if (! isBlack (m) && px >= keyLeft (m) && px < keyLeft (m) + w)
            return m;
    return -1;
}

int KeyStripLayout::nearestKey (float px) const noexcept
{
    int best = lowKey;
    float bestDist = std::numeric_limits<float>::max();
    for (int m = lowKey; m <= highKey; ++m)
    {
        const float d = std::fabs (px - keyCentre (m));
        if (d < bestDist)
        {
            bestDist = d;
            best = m;
        }
    }
    return best;
}

KeyStripLayout::Span KeyStripLayout::spanFor (int low, int high) const noexcept
{
    Span s;
    const int lo = std::max (lowKey, low);
    const int hi = std::min (highKey, high);
    if (lo > hi)
        return s;
    s.x0 = std::numeric_limits<float>::max();
    s.x1 = std::numeric_limits<float>::lowest();
    for (int m = lo; m <= hi; ++m)
    {
        s.x0 = std::min (s.x0, keyLeft (m));
        s.x1 = std::max (s.x1, keyLeft (m) + keyWidth (m));
    }
    s.visible = s.x1 > s.x0;
    return s;
}

namespace {

StripPart partOfZone (const KeyStripLayout& L, const ZoneKeySpan& z, float px, float py, int key)
{
    // Root marker: around the dot near the bottom of the root key
    if (z.root >= L.lowKey && z.root <= L.highKey)
    {
        const float half = std::max (5.0f, L.keyWidth (z.root) * 0.5f);
        if (std::fabs (px - L.keyCentre (z.root)) <= half && std::fabs (py - L.rootMarkerY (z.root)) <= 9.0f)
            return StripPart::Root;
    }
    const auto span = L.spanFor (z.low, z.high);
    if (! span.visible || py < L.y || py >= L.y + L.height)
        return StripPart::None;
    const float grab = std::min (kStripEdgeGrabPx, (span.x1 - span.x0) / 3.0f);
    const bool lowVisible = z.low >= L.lowKey;    // a clipped edge is off-screen: not grabbable
    const bool highVisible = z.high <= L.highKey;
    const float dLow = std::fabs (px - span.x0);
    const float dHigh = std::fabs (px - span.x1);
    if (lowVisible && dLow <= grab && (! highVisible || dLow <= dHigh))
        return StripPart::LowEdge;
    if (highVisible && dHigh <= grab)
        return StripPart::HighEdge;
    if (key >= z.low && key <= z.high)
        return StripPart::Body;
    return StripPart::None;
}

} // namespace

StripHit hitTestStrip (const KeyStripLayout& layout, const std::vector<ZoneKeySpan>& zones,
                       const ZoneSelection& selection, float px, float py)
{
    StripHit hit;
    hit.key = layout.keyAt (px, py);

    // Candidates: primary, other selected zones, then the rest (map order)
    std::vector<int> selected, others;
    const int primary = selection.primary();
    if (primary >= 0 && static_cast<size_t> (primary) < zones.size())
        selected.push_back (primary);
    for (size_t i : selection.indices())
        if (i < zones.size() && static_cast<int> (i) != primary)
            selected.push_back (static_cast<int> (i));
    for (size_t i = 0; i < zones.size(); ++i)
        if (! selection.contains (i))
            others.push_back (static_cast<int> (i));

    // Selected zones first (root marker, edges, body), then the rest in the same order, so a
    // neighbour's edge never steals a click inside the selected zone.
    const auto find = [&] (const std::vector<int>& group, bool handles, bool rootOnly) {
        for (int zi : group)
        {
            const auto part = partOfZone (layout, zones[static_cast<size_t> (zi)], px, py, hit.key);
            const bool isHandle = part == StripPart::Root || part == StripPart::LowEdge || part == StripPart::HighEdge;
            const bool ok = handles ? (rootOnly ? part == StripPart::Root : isHandle) : part == StripPart::Body;
            if (ok)
            {
                hit.zone = zi;
                hit.part = part;
                return true;
            }
        }
        return false;
    };
    if (find (selected, true, true) || find (selected, true, false) || find (selected, false, false)
        || find (others, true, true) || find (others, true, false) || find (others, false, false))
        return hit;
    return hit;
}

std::vector<ZoneChange> computeStripDrag (const InstrumentMap& origin, const ZoneSelection& selection,
                                          StripPart part, int anchorKey, int currentKey)
{
    std::vector<ZoneChange> out;
    const auto& sel = selection.indices();
    const int primary = selection.primary();
    if (sel.empty() || primary < 0 || static_cast<size_t> (primary) >= origin.zones.size())
        return out;
    Zone p = origin.zones[static_cast<size_t> (primary)];
    sanitizeZone (p);
    const int key = std::clamp (currentKey, zone_limits::kMinKey, zone_limits::kMaxKey);

    switch (part)
    {
        case StripPart::LowEdge:
            return offsetField (origin, sel, ZoneField::KeyLow, key - p.keyLow);
        case StripPart::HighEdge:
            return offsetField (origin, sel, ZoneField::KeyHigh, key - p.keyHigh);
        case StripPart::Root:
            return offsetField (origin, sel, ZoneField::RootKey, key - p.rootKey);
        case StripPart::Body:
            return shiftKeys (origin, sel, key - anchorKey);
        case StripPart::DrawRange:
        {
            const int a = std::clamp (anchorKey, zone_limits::kMinKey, zone_limits::kMaxKey);
            const int lo = std::min (a, key), hi = std::max (a, key);
            return mapSelected (origin, sel, [lo, hi] (const Zone& zone) {
                Zone z = zone;
                sanitizeZone (z);
                z.keyLow = lo;
                z.keyHigh = hi;
                return z;
            });
        }
        case StripPart::None:
            break;
    }
    return out;
}

const char* stripDragActionName (StripPart part, bool multiple)
{
    switch (part)
    {
        case StripPart::LowEdge:   return multiple ? "Drag low keys" : "Drag low key";
        case StripPart::HighEdge:  return multiple ? "Drag high keys" : "Drag high key";
        case StripPart::Root:      return multiple ? "Drag root keys" : "Drag root key";
        case StripPart::Body:      return multiple ? "Move zones" : "Move zone";
        case StripPart::DrawRange: return multiple ? "Draw key ranges" : "Draw key range";
        case StripPart::None:      break;
    }
    return "Zone edit";
}

std::string stripDragLabel (StripPart part, const Zone& zone, bool middleCIsC4, size_t zoneCount)
{
    const std::string enDash = "\xe2\x80\x93";
    const std::string sep = "  \xc2\xb7  ";
    const auto name = [middleCIsC4] (int midi) { return midiToNoteName (midi, middleCIsC4); };
    const std::string keys = zone.keyLow == zone.keyHigh ? name (zone.keyLow)
                                                         : name (zone.keyLow) + enDash + name (zone.keyHigh);
    std::string s;
    switch (part)
    {
        case StripPart::LowEdge:   s = "Low " + name (zone.keyLow) + sep + keys; break;
        case StripPart::HighEdge:  s = "High " + name (zone.keyHigh) + sep + keys; break;
        case StripPart::Root:      s = "Root " + name (zone.rootKey) + " (" + std::to_string (zone.rootKey) + ")"; break;
        case StripPart::Body:      s = "Keys " + keys + sep + "root " + name (zone.rootKey); break;
        case StripPart::DrawRange: s = "Keys " + keys; break;
        case StripPart::None:      s = keys; break;
    }
    if (zoneCount > 1)
        s += sep + std::to_string (zoneCount) + " zones";
    return s;
}

} // namespace looper
