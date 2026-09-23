#include "AutoMapper.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace looper {
namespace {

struct WorkingSample
{
    const SampleRef* ref = nullptr;
    FilenameTokens tokens;
    int rootKey = 60;
    PitchSource source = PitchSource::Spread;
    float confidence = 0.0f;
    std::optional<int> velocityHint; // unset → treat as mid later for sorting
    int rrIndex = 0;
    std::vector<std::string> warnings;
};

int midpointFloor(int a, int b)
{
    return static_cast<int>(std::floor((a + b) / 2.0));
}

/** Split velocity values into contiguous 1–127 ranges at midpoints. */
std::map<int, std::pair<int, int>> velocityRangesForHints(std::vector<int> hints)
{
    std::map<int, std::pair<int, int>> out;
    if (hints.empty())
        return out;

    std::sort(hints.begin(), hints.end());
    hints.erase(std::unique(hints.begin(), hints.end()), hints.end());

    if (hints.size() == 1)
    {
        out[hints[0]] = {1, 127};
        return out;
    }

    for (size_t i = 0; i < hints.size(); ++i)
    {
        int lo = 1;
        int hi = 127;
        if (i > 0)
            lo = midpointFloor(hints[i - 1], hints[i]) + 1;
        if (i + 1 < hints.size())
            hi = midpointFloor(hints[i], hints[i + 1]);
        // Clamp
        lo = std::max(1, std::min(127, lo));
        hi = std::max(1, std::min(127, hi));
        if (lo > hi)
            std::swap(lo, hi);
        out[hints[i]] = {lo, hi};
    }
    return out;
}

/** Key ranges for sorted unique roots; full 0–127 span. */
std::map<int, std::pair<int, int>> keyRangesForRoots(std::vector<int> roots, bool fullSpan)
{
    std::map<int, std::pair<int, int>> out;
    if (roots.empty())
        return out;

    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

    if (roots.size() == 1)
    {
        out[roots[0]] = fullSpan ? std::pair{0, 127} : std::pair{roots[0], roots[0]};
        return out;
    }

    for (size_t i = 0; i < roots.size(); ++i)
    {
        int lo = fullSpan ? 0 : roots.front();
        int hi = fullSpan ? 127 : roots.back();
        if (i > 0)
            lo = midpointFloor(roots[i - 1], roots[i]) + 1;
        else if (!fullSpan)
            lo = roots[i];

        if (i + 1 < roots.size())
            hi = midpointFloor(roots[i], roots[i + 1]);
        else if (!fullSpan)
            hi = roots[i];

        if (i == 0 && fullSpan)
            lo = 0;
        if (i + 1 == roots.size() && fullSpan)
            hi = 127;

        lo = std::max(0, std::min(127, lo));
        hi = std::max(0, std::min(127, hi));
        if (lo > hi)
            std::swap(lo, hi);
        out[roots[i]] = {lo, hi};
    }
    return out;
}

} // namespace

PitchDetectResult AutoMapper::detectPitchStub(const SampleRef&)
{
    // TODO: YIN (or MCM) on center window after onset; accept only high periodicity.
    PitchDetectResult r;
    r.pitchHz = std::nullopt;
    r.rootKey = std::nullopt;
    r.confidence = 0.0f;
    return r;
}

AutoMapResult AutoMapper::map(const std::vector<SampleRef>& samples, AutoMapOptions opt)
{
    AutoMapResult result;
    if (samples.empty())
        return result;

    std::vector<WorkingSample> work;
    work.reserve(samples.size());

    for (const auto& s : samples)
    {
        WorkingSample w;
        w.ref = &s;
        const std::string nameForParse = s.path.empty() ? s.displayName : s.path;
        w.tokens = parseFilenameTokens(nameForParse.empty() ? s.displayName : nameForParse);

        if (w.tokens.midiNote)
        {
            w.rootKey = *w.tokens.midiNote;
            w.source = PitchSource::Filename;
            w.confidence = 0.95f;
        }
        else
        {
            const auto detected = detectPitchStub(s);
            if (detected.rootKey && detected.confidence >= 0.7f)
            {
                w.rootKey = *detected.rootKey;
                w.source = PitchSource::Detected;
                w.confidence = detected.confidence;
            }
            else
            {
                // Unpitched → spread default root (middle C); RR siblings share this
                w.rootKey = 60;
                w.source = PitchSource::Spread;
                w.confidence = 0.25f;
                w.warnings.push_back("No reliable pitch from filename; using spread/default root");
            }
        }

        w.velocityHint = w.tokens.velocityHint;
        w.rrIndex = w.tokens.rrIndex.value_or(0);
        work.push_back(std::move(w));
    }

    // Duplicate (root, vel, rr) detection
    {
        std::map<std::tuple<int, int, int>, int> seen;
        for (auto& w : work)
        {
            const int velKey = w.velocityHint.value_or(-1);
            const auto key = std::make_tuple(w.rootKey, velKey, w.rrIndex);
            auto& count = seen[key];
            ++count;
            if (count > 1)
            {
                w.warnings.push_back("Duplicate root+velocity+RR mapping");
                result.globalWarnings.push_back(
                    "Duplicate zone identity for sample id=" + w.ref->id);
            }
        }
    }

    // Collect unique roots
    std::vector<int> roots;
    for (const auto& w : work)
        roots.push_back(w.rootKey);
    const auto keyRanges = keyRangesForRoots(roots, opt.preferFullKeyboardSpan);

    // Group by root for velocity layering
    std::map<int, std::vector<WorkingSample*>> byRoot;
    for (auto& w : work)
        byRoot[w.rootKey].push_back(&w);

    int nextRrGroup = 1;

    for (auto& [root, members] : byRoot)
    {
        const auto keyRange = keyRanges.at(root);

        // Unique velocity hints at this root (missing → use 64 as sort anchor, single-layer full range)
        std::vector<int> velHints;
        bool anyVel = false;
        for (auto* m : members)
        {
            if (m->velocityHint)
            {
                velHints.push_back(*m->velocityHint);
                anyVel = true;
            }
        }
        if (!anyVel)
            velHints.push_back(64); // placeholder for full-range single layer

        auto velRanges = velocityRangesForHints(velHints);

        // Group by velocity hint (or placeholder)
        std::map<int, std::vector<WorkingSample*>> byVel;
        for (auto* m : members)
        {
            const int vk = m->velocityHint.value_or(64);
            byVel[vk].push_back(m);
        }

        for (auto& [velKey, velMembers] : byVel)
        {
            std::pair<int, int> vr{1, 127};
            if (anyVel)
            {
                if (const auto it = velRanges.find(velKey); it != velRanges.end())
                    vr = it->second;
            }

            // Sort RR indices for determinism
            std::sort(velMembers.begin(), velMembers.end(),
                      [](const WorkingSample* a, const WorkingSample* b) {
                          if (a->rrIndex != b->rrIndex)
                              return a->rrIndex < b->rrIndex;
                          return a->ref->id < b->ref->id;
                      });

            const bool hasRr = std::any_of(velMembers.begin(), velMembers.end(),
                                           [](const WorkingSample* m) { return m->rrIndex > 0; });
            const int rrGroup = (hasRr && velMembers.size() > 1) ? nextRrGroup++ :
                                (hasRr ? nextRrGroup++ : 0);

            int autoRr = 0;
            for (auto* m : velMembers)
            {
                Zone z;
                z.sampleId = m->ref->id;
                z.rootKey = root;
                z.keyLow = keyRange.first;
                z.keyHigh = keyRange.second;
                z.velLow = vr.first;
                z.velHigh = vr.second;
                z.rrGroup = rrGroup;
                if (m->rrIndex > 0)
                    z.rrIndex = m->rrIndex;
                else if (hasRr)
                    z.rrIndex = ++autoRr;
                else
                    z.rrIndex = 0;

                result.map.zones.push_back(z);
            }
        }
    }

    // Deterministic zone order: root, velLow, rrIndex, sampleId
    std::sort(result.map.zones.begin(), result.map.zones.end(),
              [](const Zone& a, const Zone& b) {
                  if (a.rootKey != b.rootKey) return a.rootKey < b.rootKey;
                  if (a.velLow != b.velLow) return a.velLow < b.velLow;
                  if (a.rrIndex != b.rrIndex) return a.rrIndex < b.rrIndex;
                  return a.sampleId < b.sampleId;
              });

    // Reviews
    for (const auto& w : work)
    {
        SampleReview rev;
        rev.sampleId = w.ref->id;
        rev.source = w.source;
        rev.confidence = w.confidence;
        rev.warnings = w.warnings;
        rev.tokens = w.tokens;
        result.reviews.push_back(rev);
    }

    std::sort(result.reviews.begin(), result.reviews.end(),
              [](const SampleReview& a, const SampleReview& b) {
                  return a.sampleId < b.sampleId;
              });

    // Multi-instrument heuristic
    if (!roots.empty())
    {
        const int lo = *std::min_element(roots.begin(), roots.end());
        const int hi = *std::max_element(roots.begin(), roots.end());
        if (hi - lo > 36)
            result.globalWarnings.push_back("possible multi-instrument folder");
    }

    return result;
}

} // namespace looper
