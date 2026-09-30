#include "SampleRelocator.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace looper {

namespace {

fs::path toPath(const std::string& utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string fromPath(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool isSep(char c) { return c == '/' || c == '\\'; }

bool startsWithComponents(const std::vector<std::string>& path, const std::vector<std::string>& prefix)
{
    if (prefix.size() >= path.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (! SampleRelocator::equalsIgnoreCase(path[i], prefix[i]))
            return false;
    return true;
}

std::string canonicalKey(const std::string& p)
{
    return SampleRelocator::foldCase(fromPath(toPath(p).lexically_normal()));
}

void pushUnique(std::vector<std::string>& out, const std::string& p)
{
    const auto key = canonicalKey(p);
    for (const auto& e : out)
        if (canonicalKey(e) == key)
            return;
    out.push_back(p);
}

/** First predicted location (from any anchor) that exists on disk. */
bool tryPredictions(RelocationMatch& m, const std::string& anchorOriginal, const std::string& anchorNew)
{
    for (const auto& p : SampleRelocator::predictLocations(anchorOriginal, anchorNew, m.originalPath))
    {
        if (SampleRelocator::fileExists(p))
        {
            m.newPath = p;
            m.tailScore = SampleRelocator::tailMatchScore(m.originalPath, p);
            m.candidateCount = std::max(m.candidateCount, 1);
            m.ambiguous = false;
            m.alternatives.clear();
            m.viaCascade = true;
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// FolderIndex
// ---------------------------------------------------------------------------

bool FolderIndex::build(const std::string& root, const RelocatorOptions& options)
{
    byName_.clear();
    files_ = 0;
    truncated_ = false;

    std::error_code ec;
    const auto rootPath = toPath(root);
    if (root.empty() || ! fs::is_directory(rootPath, ec))
        return false;

    fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        return false;

    std::size_t visited = 0;
    for (const fs::recursive_directory_iterator end {}; it != end; it.increment(ec))
    {
        if (ec)
        {
            truncated_ = true;
            break;
        }
        if ((++visited & 0xff) == 0 && options.cancel != nullptr && options.cancel->load())
        {
            truncated_ = true;
            break;
        }

        std::error_code entryEc;
        if (it->is_directory(entryEc))
        {
            if (it.depth() >= options.maxDepth)
                it.disable_recursion_pending();
            continue;
        }
        if (! it->is_regular_file(entryEc))
            continue;

        const auto full = fromPath(it->path());
        byName_[SampleRelocator::foldCase(SampleRelocator::fileNameOf(full))].push_back(full);
        if (++files_ >= options.maxFilesScanned)
        {
            truncated_ = true;
            break;
        }
    }

    // Deterministic candidate order regardless of directory iteration order.
    for (auto& kv : byName_)
        std::sort(kv.second.begin(), kv.second.end());
    return true;
}

const std::vector<std::string>* FolderIndex::candidatesFor(const std::string& fileName) const
{
    const auto it = byName_.find(SampleRelocator::foldCase(fileName));
    return it == byName_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

std::vector<std::string> SampleRelocator::splitComponents(const std::string& path)
{
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&] {
        if (cur.empty() || cur == ".")
        {
            cur.clear();
            return;
        }
        if (cur == ".." && ! parts.empty() && parts.back() != ".."
            && ! (parts.size() == 1 && parts.back().size() == 2 && parts.back()[1] == ':'))
            parts.pop_back();
        else
            parts.push_back(cur);
        cur.clear();
    };
    for (char c : path)
    {
        if (isSep(c))
            flush();
        else
            cur.push_back(c);
    }
    flush();
    return parts;
}

std::string SampleRelocator::fileNameOf(const std::string& path)
{
    std::size_t end = path.size();
    while (end > 0 && isSep(path[end - 1]))
        --end;
    std::size_t start = end;
    while (start > 0 && ! isSep(path[start - 1]))
        --start;
    auto name = path.substr(start, end - start);
    // "C:file.wav" (drive-relative) — strip the drive
    if (start == 0 && name.size() > 2 && name[1] == ':')
        name = name.substr(2);
    return name;
}

std::string SampleRelocator::foldCase(const std::string& s)
{
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z')
        {
            out[i] = static_cast<char>(c + ('a' - 'A'));
        }
        else if (c == 0xC3 && i + 1 < out.size())
        {
            // UTF-8 U+00C0..U+00DE (À..Þ, except U+00D7 ×) -> U+00E0..U+00FE
            const auto n = static_cast<unsigned char>(out[i + 1]);
            if (n >= 0x80 && n <= 0x9E && n != 0x97)
                out[i + 1] = static_cast<char>(n + 0x20);
            ++i;
        }
    }
    return out;
}

bool SampleRelocator::equalsIgnoreCase(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && foldCase(a) == foldCase(b);
}

int SampleRelocator::tailMatchScore(const std::string& originalPath, const std::string& candidatePath)
{
    const auto a = splitComponents(originalPath);
    const auto b = splitComponents(candidatePath);
    int score = 0;
    auto ia = a.rbegin();
    auto ib = b.rbegin();
    for (; ia != a.rend() && ib != b.rend(); ++ia, ++ib)
    {
        if (! equalsIgnoreCase(*ia, *ib))
            break;
        ++score;
    }
    return score;
}

RelocationMatch SampleRelocator::chooseBest(const MissingSample& missing,
                                            const std::vector<std::string>& candidates)
{
    RelocationMatch m;
    m.sampleId = missing.sampleId;
    m.originalPath = missing.originalPath;

    const auto wantName = fileNameOf(missing.originalPath);
    struct Scored
    {
        const std::string* path;
        int score;
        bool exactCase;
    };
    std::vector<Scored> scored;
    for (const auto& c : candidates)
    {
        const int s = tailMatchScore(missing.originalPath, c);
        if (s <= 0)
            continue; // different file name
        scored.push_back({ &c, s, fileNameOf(c) == wantName });
    }
    m.candidateCount = static_cast<int>(scored.size());
    if (scored.empty())
        return m;

    std::sort(scored.begin(), scored.end(), [] (const Scored& x, const Scored& y) {
        if (x.score != y.score) return x.score > y.score;
        if (x.exactCase != y.exactCase) return x.exactCase;
        if (x.path->size() != y.path->size()) return x.path->size() < y.path->size();
        return *x.path < *y.path;
    });

    m.newPath = *scored.front().path;
    m.tailScore = scored.front().score;
    for (std::size_t i = 1; i < scored.size() && scored[i].score == m.tailScore; ++i)
        m.alternatives.push_back(*scored[i].path);
    m.ambiguous = ! m.alternatives.empty();
    return m;
}

std::vector<std::string> SampleRelocator::predictLocations(const std::string& foundOriginal,
                                                           const std::string& foundNew,
                                                           const std::string& otherOriginal)
{
    std::vector<std::string> out;
    auto oldDir = splitComponents(foundOriginal);
    if (oldDir.empty() || foundNew.empty())
        return out;
    oldDir.pop_back(); // drop file name
    const auto other = splitComponents(otherOriginal);
    const auto otherName = fileNameOf(otherOriginal);
    if (otherName.empty())
        return out;

    fs::path newAnc = toPath(foundNew).parent_path();
    // Walk up both trees in parallel: the old ancestor at depth k corresponds to the
    // new ancestor at depth k (handles same folder, sibling folders, renamed parents).
    while (! oldDir.empty() && newAnc.has_relative_path())
    {
        if (startsWithComponents(other, oldDir))
        {
            fs::path p = newAnc;
            for (std::size_t i = oldDir.size(); i < other.size(); ++i)
                p /= toPath(other[i]);
            pushUnique(out, fromPath(p));
        }
        oldDir.pop_back();
        newAnc = newAnc.parent_path();
    }

    // Flattened: same folder as the located file.
    pushUnique(out, fromPath(toPath(foundNew).parent_path() / toPath(otherName)));
    return out;
}

// ---------------------------------------------------------------------------
// Filesystem-backed search
// ---------------------------------------------------------------------------

bool SampleRelocator::fileExists(const std::string& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(toPath(path), ec);
}

bool SampleRelocator::directoryExists(const std::string& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_directory(toPath(path), ec);
}

std::string SampleRelocator::parentOf(const std::string& path)
{
    return fromPath(toPath(path).parent_path());
}

std::vector<RelocationMatch> SampleRelocator::searchFolder(const std::vector<MissingSample>& missing,
                                                           const std::string& folder,
                                                           const RelocatorOptions& options)
{
    std::vector<RelocationMatch> results;
    results.reserve(missing.size());

    FolderIndex index;
    const bool indexed = index.build(folder, options);
    for (const auto& ms : missing)
    {
        if (! indexed)
        {
            RelocationMatch m;
            m.sampleId = ms.sampleId;
            m.originalPath = ms.originalPath;
            results.push_back(std::move(m));
            continue;
        }
        static const std::vector<std::string> kNone;
        const auto* cands = index.candidatesFor(fileNameOf(ms.originalPath));
        results.push_back(chooseBest(ms, cands != nullptr ? *cands : kNone));
    }

    // Anchors: confident (unambiguous) finds.
    std::vector<std::size_t> anchors;
    for (std::size_t i = 0; i < results.size(); ++i)
        if (results[i].found() && ! results[i].ambiguous)
            anchors.push_back(i);

    // 1) Resolve ties by sibling consensus: prefer the candidate that the folder
    //    structure of confidently-found samples predicts.
    for (auto& m : results)
    {
        if (! m.ambiguous || anchors.empty())
            continue;
        std::vector<std::string> tied { m.newPath };
        tied.insert(tied.end(), m.alternatives.begin(), m.alternatives.end());
        std::vector<int> support(tied.size(), 0);
        for (auto ai : anchors)
        {
            const auto preds = predictLocations(results[ai].originalPath, results[ai].newPath, m.originalPath);
            if (preds.empty())
                continue;
            // Most specific prediction only (deepest shared ancestor).
            const auto key = canonicalKey(preds.front());
            for (std::size_t t = 0; t < tied.size(); ++t)
                if (canonicalKey(tied[t]) == key)
                    ++support[t];
        }
        const auto best = std::max_element(support.begin(), support.end());
        if (*best <= 0 || std::count(support.begin(), support.end(), *best) != 1)
            continue;
        const auto pick = tied[static_cast<std::size_t>(best - support.begin())];
        m.newPath = pick;
        m.tailScore = tailMatchScore(m.originalPath, pick);
        m.ambiguous = false;
        m.alternatives.clear();
        m.viaCascade = true;
    }

    // 2) Files not under `folder` (e.g. user picked a sub-folder): mirror the
    //    structure of the samples that were found.
    for (auto& m : results)
    {
        if (m.found())
            continue;
        for (auto ai : anchors)
            if (tryPredictions(m, results[ai].originalPath, results[ai].newPath))
                break;
    }
    return results;
}

std::vector<RelocationMatch> SampleRelocator::cascadeFrom(const std::string& foundOriginal,
                                                          const std::string& foundNew,
                                                          const std::vector<MissingSample>& remaining,
                                                          const RelocatorOptions& options)
{
    std::vector<RelocationMatch> results;
    results.reserve(remaining.size());
    bool anyLeft = false;
    for (const auto& ms : remaining)
    {
        RelocationMatch m;
        m.sampleId = ms.sampleId;
        m.originalPath = ms.originalPath;
        if (! tryPredictions(m, foundOriginal, foundNew))
            anyLeft = true;
        results.push_back(std::move(m));
    }
    if (! anyLeft)
        return results;

    // Fall back to a recursive search of the folder the located file lives in.
    FolderIndex index;
    if (! index.build(parentOf(foundNew), options))
        return results;
    for (auto& m : results)
    {
        if (m.found())
            continue;
        const auto* cands = index.candidatesFor(fileNameOf(m.originalPath));
        if (cands == nullptr)
            continue;
        auto best = chooseBest({ m.sampleId, m.originalPath }, *cands);
        if (best.found())
        {
            best.viaCascade = true;
            m = std::move(best);
        }
    }
    return results;
}

} // namespace looper
