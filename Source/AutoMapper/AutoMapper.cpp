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
    PitchSource source = PitchSource::Unpitched;
    float confidence = 0.0f;
    float tuneCents = 0.0f;
    std::optional<PitchAnalysis> pitch;
    std::optional<int> velocityHint; // unset → treat as mid later for sorting
    int rrIndex = 0;
    std::vector<std::string> warnings;
    std::optional<PitchMismatch> mismatch;
    std::optional<int> filenameNote;
    bool filenameOverridden = false;
    bool chromatic = false;          // unpitched, placed by the chromatic fallback
};

bool usablePitch(const std::optional<PitchAnalysis>& a)
{
    return a && a->analysed && !a->unpitched && a->midiNote >= 0 && a->midiNote <= 127;
}

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

std::string formatCents(float cents)
{
    const int c = static_cast<int>(std::lround(cents));
    if (c == 0)
        return "0 ct";
    // U+2212 MINUS SIGN for negatives, '+' for positives
    return (c < 0 ? std::string("\xE2\x88\x92") + std::to_string(-c) : "+" + std::to_string(c)) + " ct";
}

} // namespace

std::string describeInterval(int semitones)
{
    const int mag = semitones < 0 ? -semitones : semitones;
    const std::string dir = semitones < 0 ? "lower" : "higher";
    if (mag == 0)
        return "the same";
    if (mag % 12 == 0)
    {
        const int oct = mag / 12;
        return (oct == 1 ? std::string("an octave ") : std::to_string(oct) + " octaves ") + dir;
    }
    return std::to_string(mag) + (mag == 1 ? " semitone " : " semitones ") + dir;
}

std::string describePitch(const SampleReview& r, bool middleCIsC4)
{
    const int pct = static_cast<int>(std::lround(r.confidence * 100.0f));
    const auto name = [middleCIsC4](int n) { return midiToNoteName(n, middleCIsC4); };
    switch (r.source)
    {
        case PitchSource::Filename:
            return "Filename " + name(r.rootKey);
        case PitchSource::Detected:
        {
            const float cents = r.pitch ? r.pitch->cents : -r.tuneCents;
            std::string out = "Detected " + name(r.rootKey) + " (" + formatCents(cents) + ") "
                            + std::to_string(pct) + "%";
            if (r.filenameOverridden && r.filenameNote)
                out += " (filename " + name(*r.filenameNote) + " overridden)";
            return out;
        }
        case PitchSource::Unpitched:
            break;
    }
    if (r.pitch && r.pitch->analysed)
        return "Unpitched (" + r.pitch->reason + ") \xE2\x86\x92 " + name(r.rootKey);
    return "No pitch \xE2\x86\x92 " + name(r.rootKey);
}

bool AutoMapper::canUseDetected(const SampleReview& r)
{
    return r.source == PitchSource::Filename && r.mismatch.has_value() && usablePitch(r.pitch);
}

AutoMapResult AutoMapper::map(const std::vector<SampleRef>& samples, AutoMapOptions opt,
                              const PitchAnalysisMap* analyses)
{
    AutoMapResult result;
    result.middleCIsC4 = opt.middleCIsC4;
    if (samples.empty())
        return result;

    const auto name = [&opt](int n) { return midiToNoteName(n, opt.middleCIsC4); };
    const bool chromaticFallback = opt.unpitchedFallback == AutoMapOptions::UnpitchedFallback::Chromatic;

    std::vector<WorkingSample> work;
    work.reserve(samples.size());

    for (const auto& s : samples)
    {
        WorkingSample w;
        w.ref = &s;
        const std::string nameForParse = s.path.empty() ? s.displayName : s.path;
        w.tokens = parseFilenameTokens(nameForParse.empty() ? s.displayName : nameForParse,
                                       opt.middleCIsC4);

        if (analyses)
            if (const auto it = analyses->find(s.id); it != analyses->end() && it->second.analysed)
                w.pitch = it->second;
        const bool detectedOk = usablePitch(w.pitch);

        if (w.tokens.midiNote)
        {
            w.filenameNote = *w.tokens.midiNote;

            // Filename/audio mismatch: only confident detections, >= 1 semitone apart.
            if (detectedOk && w.pitch->confidence >= opt.mismatchMinConfidence
                && w.pitch->midiNote != *w.tokens.midiNote)
            {
                PitchMismatch mm;
                mm.filenameNote = *w.tokens.midiNote;
                mm.detectedNote = w.pitch->midiNote;
                mm.semitones = mm.detectedNote - mm.filenameNote;
                mm.confidence = w.pitch->confidence;
                mm.octave = mm.semitones % 12 == 0;
                mm.text = "Filename " + name(mm.filenameNote) + ", audio sounds "
                        + (mm.octave ? describeInterval(mm.semitones) + " (" + name(mm.detectedNote) + ")"
                                     : name(mm.detectedNote));
                w.mismatch = mm;
            }
        }

        const bool overrideFilename = w.tokens.midiNote && detectedOk
                                   && opt.useDetectedFor.count(s.id) > 0;

        if (w.tokens.midiNote && !overrideFilename)
        {
            w.rootKey = *w.tokens.midiNote;
            w.source = PitchSource::Filename;
            w.confidence = 0.95f;
            if (w.mismatch)
                w.warnings.push_back(w.mismatch->text); // filename still wins; no auto change
        }
        else if (detectedOk)
        {
            w.rootKey = w.pitch->midiNote;
            w.source = PitchSource::Detected;
            w.confidence = w.pitch->confidence;
            w.filenameOverridden = overrideFilename;
            if (opt.applyDetectedFineTune)
                w.tuneCents = -w.pitch->cents; // sample is +x ct sharp → play it x ct lower
            if (w.confidence < opt.lowConfidenceWarnBelow)
            {
                w.warnings.push_back("Low pitch-detection confidence ("
                                     + std::to_string(static_cast<int>(std::lround(w.confidence * 100.0f)))
                                     + "%); check root key");
            }
        }
        else
        {
            w.source = PitchSource::Unpitched;
            if (chromaticFallback)
            {
                // Key assigned below (drum-kit spread); expected for percussion → no warning.
                w.chromatic = true;
                w.confidence = w.pitch ? w.pitch->confidence : 0.0f;
            }
            else
            {
                // Legacy "equal spread + warn": default root (middle C); RR siblings share it.
                w.rootKey = 60;
                w.confidence = 0.25f;
                if (w.pitch)
                    w.warnings.push_back("No note in filename and audio is unpitched ("
                                         + w.pitch->reason + "); using spread/default root");
                else
                    w.warnings.push_back("No reliable pitch from filename; using spread/default root");
            }
        }

        w.velocityHint = w.tokens.velocityHint;
        w.rrIndex = w.tokens.rrIndex.value_or(0);
        work.push_back(std::move(w));
    }

    // --- Chromatic (drum-kit) placement of unpitched sounds ---
    std::set<int> chromaticKeys;
    {
        std::set<int> used;
        for (const auto& w : work)
            if (!w.chromatic)
                used.insert(w.rootKey);

        // Velocity layers / RR alternates of one sound share a group (and so a key).
        std::map<std::string, std::vector<WorkingSample*>> groups;
        for (auto& w : work)
            if (w.chromatic)
                groups[layerGroupName(w.tokens)].push_back(&w);

        std::vector<std::pair<std::string, std::vector<WorkingSample*>*>> order;
        for (auto& [g, members] : groups)
            order.emplace_back(g, &members);
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
            if (naturalLess(a.first, b.first)) return true;
            if (naturalLess(b.first, a.first)) return false;
            return a.first < b.first;
        });

        const int start = std::clamp(opt.unpitchedStartNote, 0, 127);
        int up = start;
        int down = start - 1;
        for (auto& [g, members] : order)
        {
            while (up <= 127 && used.count(up)) ++up;
            int key = -1;
            if (up <= 127)
                key = up++;
            else
            {
                while (down >= 0 && used.count(down)) --down;
                if (down >= 0) key = down--;
            }
            const bool outOfKeys = key < 0;
            if (outOfKeys)
                key = 127;
            used.insert(key);
            chromaticKeys.insert(key);
            for (auto* m : *members)
            {
                m->rootKey = key;
                if (outOfKeys)
                    m->warnings.push_back("No free key left for unpitched sound; stacked on " + name(key));
            }
        }
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

    // Key ranges: pitched (and legacy fixed-root) roots split the keyboard at midpoints;
    // chromatic drum keys are single keys and pitched spans are clipped around their block.
    std::vector<int> roots;
    for (const auto& w : work)
        if (!w.chromatic)
            roots.push_back(w.rootKey);
    auto keyRanges = keyRangesForRoots(roots, opt.preferFullKeyboardSpan);
    if (!chromaticKeys.empty())
    {
        const int uLo = *chromaticKeys.begin();
        const int uHi = *chromaticKeys.rbegin();
        for (auto& [root, range] : keyRanges)
        {
            if (root < uLo)
                range.second = std::min(range.second, uLo - 1);
            else if (root > uHi)
                range.first = std::max(range.first, uHi + 1);
            else
                range = {root, root}; // pitched root inside the drum block
            if (range.first > range.second)
                range = {root, root};
        }
        for (int k : chromaticKeys)
            keyRanges[k] = {k, k};
    }

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
                z.tuneCents = m->tuneCents;
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
        rev.rootKey = w.rootKey;
        rev.tuneCents = w.tuneCents;
        rev.pitch = w.pitch;
        rev.mismatch = w.mismatch;
        rev.filenameNote = w.filenameNote;
        rev.filenameOverridden = w.filenameOverridden;
        rev.chromaticKey = w.chromatic;
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

void applyPitchMetadata(std::vector<SampleRef>& refs, const AutoMapResult& result)
{
    for (auto& ref : refs)
    {
        const auto it = std::find_if(result.reviews.begin(), result.reviews.end(),
                                     [&](const SampleReview& r) { return r.sampleId == ref.id; });
        if (it == result.reviews.end())
            continue;
        ref.pitchSource = it->source;
        ref.pitchMismatch = it->mismatch.has_value();
        if (it->pitch && it->pitch->analysed)
        {
            ref.pitchConfidence = it->pitch->confidence;
            if (usablePitch(it->pitch))
            {
                ref.detectedPitchHz = it->pitch->f0Hz;
                ref.detectedRootKey = it->pitch->midiNote;
                ref.detectedCents = it->pitch->cents;
            }
            else
            {
                ref.detectedPitchHz.reset();
                ref.detectedRootKey.reset();
                ref.detectedCents.reset();
            }
        }
    }
}

} // namespace looper
