#pragma once

#include "../AutoMapper/AutoMapper.h"
#include "../InstrumentMap/InstrumentMap.h"

#include <algorithm>
#include <set>

namespace looper {

inline void commitAutoMapToInstrument(InstrumentMap& dest, const AutoMapResult& result)
{
    dest.zones = result.map.zones;
    if (dest.polyphonyLimit <= 0)
        dest.polyphonyLimit = 64;
}

inline int countUniqueRoots(const InstrumentMap& map)
{
    std::set<int> roots;
    for (const auto& z : map.zones)
        roots.insert(z.rootKey);
    return static_cast<int>(roots.size());
}

inline int maxRrDepth(const InstrumentMap& map)
{
    int maxDepth = 0;
    for (const auto& z : map.zones)
        if (z.rrGroup > 0)
            maxDepth = std::max(maxDepth, z.rrIndex);
    return maxDepth;
}

} // namespace looper
