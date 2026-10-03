#include "ZoneSelection.h"

#include <algorithm>
#include <cmath>

namespace looper {

bool ZoneSelection::contains (size_t index) const noexcept
{
    return std::binary_search (indices_.begin(), indices_.end(), index);
}

void ZoneSelection::clear() noexcept
{
    indices_.clear();
    primary_ = -1;
    anchor_ = -1;
}

void ZoneSelection::selectOnly (size_t index)
{
    indices_.assign (1, index);
    primary_ = static_cast<int> (index);
    anchor_ = primary_;
}

void ZoneSelection::toggle (size_t index)
{
    const auto it = std::lower_bound (indices_.begin(), indices_.end(), index);
    if (it != indices_.end() && *it == index)
    {
        const auto pos = it - indices_.begin();
        indices_.erase (it);
        if (primary_ == static_cast<int> (index))
        {
            if (indices_.empty())
                primary_ = -1;
            else
                primary_ = static_cast<int> (indices_[static_cast<size_t> (pos > 0 ? pos - 1 : 0)]);
        }
        if (anchor_ == static_cast<int> (index))
            anchor_ = primary_;
        return;
    }
    indices_.insert (it, index);
    primary_ = static_cast<int> (index);
    anchor_ = primary_;
}

void ZoneSelection::selectRange (const std::vector<size_t>& order, size_t index)
{
    const auto to = std::find (order.begin(), order.end(), index);
    const auto from = anchor_ >= 0 ? std::find (order.begin(), order.end(), static_cast<size_t> (anchor_)) : order.end();
    if (to == order.end() || from == order.end())
    {
        selectOnly (index);
        return;
    }
    const auto lo = std::min (from, to);
    const auto hi = std::max (from, to);
    indices_.assign (lo, hi + 1);
    normalise();
    primary_ = static_cast<int> (index);
}

void ZoneSelection::selectAll (size_t count)
{
    indices_.resize (count);
    for (size_t i = 0; i < count; ++i)
        indices_[i] = i;
    if (count == 0)
    {
        primary_ = anchor_ = -1;
        return;
    }
    if (primary_ < 0 || static_cast<size_t> (primary_) >= count)
        primary_ = 0;
    if (anchor_ < 0 || static_cast<size_t> (anchor_) >= count)
        anchor_ = primary_;
}

void ZoneSelection::set (std::vector<size_t> indices, int primary)
{
    indices_ = std::move (indices);
    normalise();
    if (primary >= 0 && contains (static_cast<size_t> (primary)))
        primary_ = primary;
    else
        primary_ = indices_.empty() ? -1 : static_cast<int> (indices_.front());
    if (anchor_ < 0 || ! contains (static_cast<size_t> (anchor_)))
        anchor_ = primary_;
}

void ZoneSelection::setPrimary (size_t index)
{
    if (contains (index))
        primary_ = static_cast<int> (index);
}

void ZoneSelection::prune (size_t count)
{
    indices_.erase (std::remove_if (indices_.begin(), indices_.end(), [count] (size_t i) { return i >= count; }),
                    indices_.end());
    if (primary_ >= 0 && static_cast<size_t> (primary_) >= count)
        primary_ = indices_.empty() ? -1 : static_cast<int> (indices_.back());
    if (anchor_ >= 0 && static_cast<size_t> (anchor_) >= count)
        anchor_ = primary_;
}

void ZoneSelection::normalise()
{
    std::sort (indices_.begin(), indices_.end());
    indices_.erase (std::unique (indices_.begin(), indices_.end()), indices_.end());
}

void applySelectionClick (ZoneSelection& selection, size_t index, ClickModifier mod,
                          const std::vector<size_t>& displayOrder)
{
    switch (mod)
    {
        case ClickModifier::None:   selection.selectOnly (index); break;
        case ClickModifier::Toggle: selection.toggle (index); break;
        case ClickModifier::Range:  selection.selectRange (displayOrder, index); break;
    }
}

FieldSummary summarizeField (const InstrumentMap& map, const std::vector<size_t>& indices,
                             ZoneField field, int primary)
{
    FieldSummary s;
    const bool fractional = field == ZoneField::TuneCents || field == ZoneField::GainDb;
    const double eps = fractional ? 0.005 : 0.5;
    bool haveFirst = false;
    if (primary >= 0 && static_cast<size_t> (primary) < map.zones.size()
        && std::find (indices.begin(), indices.end(), static_cast<size_t> (primary)) != indices.end())
    {
        s.first = zoneFieldValue (map.zones[static_cast<size_t> (primary)], field);
        haveFirst = true;
    }
    for (size_t i : indices)
    {
        if (i >= map.zones.size())
            continue;
        const double v = zoneFieldValue (map.zones[i], field);
        if (s.count == 0)
        {
            s.minValue = s.maxValue = v;
            if (! haveFirst)
                s.first = v;
        }
        else
        {
            s.minValue = std::min (s.minValue, v);
            s.maxValue = std::max (s.maxValue, v);
        }
        ++s.count;
    }
    s.mixed = s.count > 1 && (s.maxValue - s.minValue) > eps;
    return s;
}

bool fieldEditsRelative (ZoneField field)
{
    switch (field)
    {
        case ZoneField::RootKey:
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:
        case ZoneField::TuneCents:
        case ZoneField::GainDb:
            return true;
        case ZoneField::VelLow:
        case ZoneField::VelHigh:
        case ZoneField::RrGroup:
        case ZoneField::RrIndex:
            return false;
    }
    return false;
}

std::vector<ZoneChange> offsetField (const InstrumentMap& origin, const std::vector<size_t>& indices,
                                     ZoneField field, double delta)
{
    return mapSelected (origin, indices, [&] (const Zone& z) {
        return withZoneField (z, field, zoneFieldValue (z, field) + delta);
    });
}

std::vector<ZoneChange> setFieldAll (const InstrumentMap& origin, const std::vector<size_t>& indices,
                                     ZoneField field, double value)
{
    return mapSelected (origin, indices, [&] (const Zone& z) { return withZoneField (z, field, value); });
}

int clampKeyShift (const InstrumentMap& origin, const std::vector<size_t>& indices, int semis)
{
    bool any = false;
    int minLow = zone_limits::kMaxKey, maxHigh = zone_limits::kMinKey;
    for (size_t i : indices)
    {
        if (i >= origin.zones.size())
            continue;
        Zone z = origin.zones[i];
        sanitizeZone (z);
        minLow = std::min (minLow, z.keyLow);
        maxHigh = std::max (maxHigh, z.keyHigh);
        any = true;
    }
    if (! any)
        return 0;
    return std::clamp (semis, zone_limits::kMinKey - minLow, zone_limits::kMaxKey - maxHigh);
}

std::vector<ZoneChange> shiftKeys (const InstrumentMap& origin, const std::vector<size_t>& indices, int semis)
{
    const int d = clampKeyShift (origin, indices, semis);
    return mapSelected (origin, indices, [d] (const Zone& zone) {
        Zone z = zone;
        sanitizeZone (z);
        z.keyLow += d;
        z.keyHigh += d;
        z.rootKey = std::clamp (z.rootKey + d, zone_limits::kMinKey, zone_limits::kMaxKey);
        return z;
    });
}

MultiEditText classifyMultiEditText (const std::string& text)
{
    size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t'))
        ++b;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t'))
        --e;
    MultiEditText out;
    if (b < e && text[b] == '=')
    {
        out.relative = false;
        out.body = text.substr (b + 1, e - b - 1);
        return out;
    }
    out.relative = b < e && (text[b] == '+' || text[b] == '-');
    out.body = text.substr (b, e - b);
    return out;
}

} // namespace looper
