#include "ZoneEditor.h"

#include <algorithm>
#include <cmath>

namespace looper {

namespace {

int clampInt(double v, int lo, int hi)
{
    if (!std::isfinite(v))
        return lo;
    const double r = std::round(v);
    if (r < static_cast<double>(lo)) return lo;
    if (r > static_cast<double>(hi)) return hi;
    return static_cast<int>(r);
}

float clampFloat(double v, float lo, float hi)
{
    if (!std::isfinite(v))
        return 0.0f < lo ? lo : (0.0f > hi ? hi : 0.0f);
    // Store at 0.01 resolution so UI round trips and JSON stay stable.
    const double r = std::round(v * 100.0) / 100.0;
    return static_cast<float>(std::clamp(r, static_cast<double>(lo), static_cast<double>(hi)));
}

bool nearlyEqual(float a, float b, float eps = 1.0e-4f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

const char* zoneFieldName(ZoneField field)
{
    switch (field)
    {
        case ZoneField::RootKey:   return "Root key";
        case ZoneField::KeyLow:    return "Low key";
        case ZoneField::KeyHigh:   return "High key";
        case ZoneField::VelLow:    return "Low velocity";
        case ZoneField::VelHigh:   return "High velocity";
        case ZoneField::TuneCents: return "Fine tune";
        case ZoneField::GainDb:    return "Gain";
        case ZoneField::RrGroup:   return "RR group";
        case ZoneField::RrIndex:   return "RR alternate";
    }
    return "Zone";
}

double zoneFieldValue(const Zone& z, ZoneField field)
{
    switch (field)
    {
        case ZoneField::RootKey:   return z.rootKey;
        case ZoneField::KeyLow:    return z.keyLow;
        case ZoneField::KeyHigh:   return z.keyHigh;
        case ZoneField::VelLow:    return z.velLow;
        case ZoneField::VelHigh:   return z.velHigh;
        case ZoneField::TuneCents: return z.tuneCents;
        case ZoneField::GainDb:    return z.gainDb;
        case ZoneField::RrGroup:   return z.rrGroup;
        case ZoneField::RrIndex:   return z.rrIndex;
    }
    return 0.0;
}

void zoneFieldRange(ZoneField field, double& minOut, double& maxOut)
{
    using namespace zone_limits;
    switch (field)
    {
        case ZoneField::RootKey:
        case ZoneField::KeyLow:
        case ZoneField::KeyHigh:   minOut = kMinKey; maxOut = kMaxKey; return;
        case ZoneField::VelLow:
        case ZoneField::VelHigh:   minOut = kMinVel; maxOut = kMaxVel; return;
        case ZoneField::TuneCents: minOut = kMinTuneCents; maxOut = kMaxTuneCents; return;
        case ZoneField::GainDb:    minOut = kMinGainDb; maxOut = kMaxGainDb; return;
        case ZoneField::RrGroup:   minOut = 0; maxOut = kMaxRrGroup; return;
        case ZoneField::RrIndex:   minOut = 0; maxOut = kMaxRrIndex; return;
    }
    minOut = 0.0;
    maxOut = 0.0;
}

bool sanitizeZone(Zone& z)
{
    using namespace zone_limits;
    const Zone before = z;
    z.rootKey = clampInt(z.rootKey, kMinKey, kMaxKey);
    z.keyLow = clampInt(z.keyLow, kMinKey, kMaxKey);
    z.keyHigh = clampInt(z.keyHigh, kMinKey, kMaxKey);
    if (z.keyLow > z.keyHigh)
        std::swap(z.keyLow, z.keyHigh);
    z.velLow = clampInt(z.velLow, kMinVel, kMaxVel);
    z.velHigh = clampInt(z.velHigh, kMinVel, kMaxVel);
    if (z.velLow > z.velHigh)
        std::swap(z.velLow, z.velHigh);
    z.rrGroup = clampInt(z.rrGroup, 0, kMaxRrGroup);
    z.rrIndex = clampInt(z.rrIndex, 0, kMaxRrIndex);
    if (!std::isfinite(z.tuneCents)) z.tuneCents = 0.0f;
    if (!std::isfinite(z.gainDb)) z.gainDb = 0.0f;
    z.tuneCents = std::clamp(z.tuneCents, kMinTuneCents, kMaxTuneCents);
    z.gainDb = std::clamp(z.gainDb, kMinGainDb, kMaxGainDb);
    return !sameZoneSettings(before, z);
}

Zone withZoneField(const Zone& zone, ZoneField field, double value)
{
    using namespace zone_limits;
    Zone z = zone;
    sanitizeZone(z);
    switch (field)
    {
        case ZoneField::RootKey:
            z.rootKey = clampInt(value, kMinKey, kMaxKey);
            break;
        case ZoneField::KeyLow:
            z.keyLow = clampInt(value, kMinKey, kMaxKey);
            z.keyHigh = std::max(z.keyHigh, z.keyLow);
            break;
        case ZoneField::KeyHigh:
            z.keyHigh = clampInt(value, kMinKey, kMaxKey);
            z.keyLow = std::min(z.keyLow, z.keyHigh);
            break;
        case ZoneField::VelLow:
            z.velLow = clampInt(value, kMinVel, kMaxVel);
            z.velHigh = std::max(z.velHigh, z.velLow);
            break;
        case ZoneField::VelHigh:
            z.velHigh = clampInt(value, kMinVel, kMaxVel);
            z.velLow = std::min(z.velLow, z.velHigh);
            break;
        case ZoneField::TuneCents:
            z.tuneCents = clampFloat(value, kMinTuneCents, kMaxTuneCents);
            break;
        case ZoneField::GainDb:
            z.gainDb = clampFloat(value, kMinGainDb, kMaxGainDb);
            break;
        case ZoneField::RrGroup:
            z.rrGroup = clampInt(value, 0, kMaxRrGroup);
            break;
        case ZoneField::RrIndex:
            z.rrIndex = clampInt(value, 0, kMaxRrIndex);
            break;
    }
    return z;
}

bool sameZoneSettings(const Zone& a, const Zone& b)
{
    return a.sampleId == b.sampleId
        && a.rootKey == b.rootKey
        && a.keyLow == b.keyLow && a.keyHigh == b.keyHigh
        && a.velLow == b.velLow && a.velHigh == b.velHigh
        && a.rrGroup == b.rrGroup && a.rrIndex == b.rrIndex
        && nearlyEqual(a.tuneCents, b.tuneCents)
        && a.coarseTranspose == b.coarseTranspose
        && nearlyEqual(a.gainDb, b.gainDb)
        && nearlyEqual(a.pan, b.pan)
        && a.sampleStart == b.sampleStart && a.sampleEnd == b.sampleEnd
        && a.autoValues == b.autoValues;
}

ZoneAutoValues captureAutoValues(const Zone& z)
{
    ZoneAutoValues a;
    a.rootKey = z.rootKey;
    a.keyLow = z.keyLow;
    a.keyHigh = z.keyHigh;
    a.velLow = z.velLow;
    a.velHigh = z.velHigh;
    a.rrGroup = z.rrGroup;
    a.rrIndex = z.rrIndex;
    a.tuneCents = z.tuneCents;
    a.gainDb = z.gainDb;
    return a;
}

void stampAutoValues(InstrumentMap& map)
{
    for (auto& z : map.zones)
        z.autoValues = captureAutoValues(z);
}

Zone resetZoneToAuto(const Zone& zone)
{
    Zone z = zone;
    if (!z.autoValues)
        return z;
    const auto& a = *z.autoValues;
    z.rootKey = a.rootKey;
    z.keyLow = a.keyLow;
    z.keyHigh = a.keyHigh;
    z.velLow = a.velLow;
    z.velHigh = a.velHigh;
    z.rrGroup = a.rrGroup;
    z.rrIndex = a.rrIndex;
    z.tuneCents = a.tuneCents;
    z.gainDb = a.gainDb;
    sanitizeZone(z);
    return z;
}

bool isZoneEditedFromAuto(const Zone& zone)
{
    if (!zone.autoValues)
        return false;
    const auto cur = captureAutoValues(zone);
    const auto& a = *zone.autoValues;
    return cur.rootKey != a.rootKey || cur.keyLow != a.keyLow || cur.keyHigh != a.keyHigh
        || cur.velLow != a.velLow || cur.velHigh != a.velHigh
        || cur.rrGroup != a.rrGroup || cur.rrIndex != a.rrIndex
        || !nearlyEqual(cur.tuneCents, a.tuneCents, 0.005f)
        || !nearlyEqual(cur.gainDb, a.gainDb, 0.005f);
}

std::optional<DetectedPitchInfo> detectedPitchFor(const SampleRef& s)
{
    if (!s.detectedRootKey)
        return std::nullopt;
    DetectedPitchInfo p;
    p.rootKey = std::clamp(*s.detectedRootKey, zone_limits::kMinKey, zone_limits::kMaxKey);
    p.cents = s.detectedCents.value_or(0.0f);
    if (!std::isfinite(p.cents))
        p.cents = 0.0f;
    p.cents = std::clamp(p.cents, -50.0f, 50.0f);
    p.confidence = s.pitchConfidence.value_or(1.0f);
    return p;
}

Zone withDetectedPitch(const Zone& zone, const DetectedPitchInfo& pitch)
{
    Zone z = withZoneField(zone, ZoneField::RootKey, pitch.rootKey);
    return withZoneField(z, ZoneField::TuneCents, -static_cast<double>(pitch.cents));
}

bool zoneUsesDetectedPitch(const Zone& zone, const DetectedPitchInfo& pitch)
{
    const Zone target = withDetectedPitch(zone, pitch);
    return zone.rootKey == target.rootKey && nearlyEqual(zone.tuneCents, target.tuneCents, 0.05f);
}

std::vector<size_t> findOverlaps(const InstrumentMap& map, size_t index)
{
    std::vector<size_t> out;
    if (index >= map.zones.size())
        return out;
    const Zone& a = map.zones[index];
    for (size_t i = 0; i < map.zones.size(); ++i)
    {
        if (i == index)
            continue;
        const Zone& b = map.zones[i];
        if (a.rrGroup != 0 && a.rrGroup == b.rrGroup)
            continue; // round-robin alternates are meant to stack
        const bool keys = a.keyLow <= b.keyHigh && b.keyLow <= a.keyHigh;
        const bool vels = a.velLow <= b.velHigh && b.velLow <= a.velHigh;
        if (keys && vels)
            out.push_back(i);
    }
    return out;
}

std::optional<ZoneEdit> makeZoneEdit(const InstrumentMap& map, size_t index, const Zone& proposed)
{
    if (index >= map.zones.size())
        return std::nullopt;
    const Zone& current = map.zones[index];
    if (current.sampleId != proposed.sampleId)
        return std::nullopt;
    Zone after = proposed;
    sanitizeZone(after);
    if (sameZoneSettings(current, after))
        return std::nullopt;
    ZoneEdit e;
    e.index = index;
    e.sampleId = current.sampleId;
    e.before = current;
    e.after = std::move(after);
    return e;
}

bool replaceZoneInMap(InstrumentMap& map, size_t index, const std::string& sampleId, const Zone& zone)
{
    if (index >= map.zones.size() || map.zones[index].sampleId != sampleId || zone.sampleId != sampleId)
        return false;
    map.zones[index] = zone;
    return true;
}

bool applyZoneEdit(InstrumentMap& map, const ZoneEdit& edit, bool forward)
{
    return replaceZoneInMap(map, edit.index, edit.sampleId, forward ? edit.after : edit.before);
}

std::vector<ZoneEdit> makeZoneEdits(const InstrumentMap& map, const std::vector<ZoneChange>& changes)
{
    std::vector<ZoneEdit> out;
    out.reserve(changes.size());
    for (const auto& c : changes)
    {
        if (c.index >= map.zones.size() || map.zones[c.index].sampleId != c.zone.sampleId)
            return {};
        // A zone listed twice: the later proposal wins, measured against the original zone.
        auto dup = std::find_if(out.begin(), out.end(), [&](const ZoneEdit& e) { return e.index == c.index; });
        if (dup != out.end())
            out.erase(dup);
        if (auto e = makeZoneEdit(map, c.index, c.zone))
            out.push_back(std::move(*e));
    }
    return out;
}

bool applyZoneEdits(InstrumentMap& map, const std::vector<ZoneEdit>& edits, bool forward)
{
    for (const auto& e : edits)
        if (e.index >= map.zones.size() || map.zones[e.index].sampleId != e.sampleId)
            return false;
    if (forward)
    {
        for (const auto& e : edits)
            map.zones[e.index] = e.after;
    }
    else
    {
        for (auto it = edits.rbegin(); it != edits.rend(); ++it)
            map.zones[it->index] = it->before;
    }
    return true;
}

void auditionNoteFor(const Zone& zone, int& noteOut, int& velocityOut)
{
    Zone z = zone;
    sanitizeZone(z);
    noteOut = std::clamp(z.rootKey, z.keyLow, z.keyHigh);
    velocityOut = std::clamp(100, z.velLow, z.velHigh);
}

} // namespace looper
