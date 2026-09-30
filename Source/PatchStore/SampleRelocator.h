#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace looper {

/**
 * "Relocate missing samples" — matching / search logic (no JUCE, std::filesystem only).
 *
 * All paths are UTF-8 std::strings. Original paths may come from any OS
 * (POSIX, Windows drive letters "C:\\…", UNC "\\\\server\\share\\…"); candidate
 * paths are real paths on this machine.
 *
 * Matching is by file name, case-insensitive (ASCII + Latin-1 letters in UTF-8).
 * When several files share the name, the one whose trailing sub-path best
 * matches the original ("tail score") wins; ties are reported as ambiguous.
 */
struct MissingSample
{
    std::string sampleId;
    std::string originalPath;
};

struct RelocationMatch
{
    std::string sampleId;
    std::string originalPath;
    /** Chosen replacement on disk (empty = not found). */
    std::string newPath;
    /** Number of trailing path components (file name included) equal to the original. */
    int tailScore = 0;
    /** How many files with a matching name were considered. */
    int candidateCount = 0;
    /** More than one candidate shared the best score; newPath is a deterministic best guess. */
    bool ambiguous = false;
    /** Found by following the folder structure of another relocated sample. */
    bool viaCascade = false;
    /** Other candidates that tied with newPath (only when ambiguous). */
    std::vector<std::string> alternatives;

    bool found() const noexcept { return ! newPath.empty(); }
};

struct RelocatorOptions
{
    /** Stop indexing after this many files (protects against picking "/" or a whole drive). */
    std::size_t maxFilesScanned = 250000;
    /** Maximum recursion depth below the search root. */
    int maxDepth = 32;
    /** Optional cancellation flag polled while scanning. */
    const std::atomic<bool>* cancel = nullptr;
};

/** Recursive file-name index of one folder (lower-cased name -> full paths). */
class FolderIndex
{
public:
    /** Scan root recursively. Returns false if root is not a readable directory. */
    bool build(const std::string& root, const RelocatorOptions& options = {});

    /** Candidates whose file name equals fileName case-insensitively (nullptr if none). */
    const std::vector<std::string>* candidatesFor(const std::string& fileName) const;

    std::size_t fileCount() const noexcept { return files_; }
    /** True if the scan hit maxFilesScanned / was cancelled before finishing. */
    bool truncated() const noexcept { return truncated_; }

private:
    std::unordered_map<std::string, std::vector<std::string>> byName_;
    std::size_t files_ = 0;
    bool truncated_ = false;
};

class SampleRelocator
{
public:
    // --- Pure string helpers (no filesystem access) ---

    /** Split on '/' and '\\'; drops empty parts and "."; resolves ".." lexically. Drive "C:" kept as a part. */
    static std::vector<std::string> splitComponents(const std::string& path);

    /** Last path component ("" if none). Works for Windows and POSIX separators. */
    static std::string fileNameOf(const std::string& path);

    /** Case-fold for comparisons: ASCII A-Z and UTF-8 Latin-1 capitals (À..Þ) to lower case. */
    static std::string foldCase(const std::string& s);

    static bool equalsIgnoreCase(const std::string& a, const std::string& b);

    /**
     * Count of equal trailing components (case-insensitive), file name first.
     * 0 when the file names differ.
     */
    static int tailMatchScore(const std::string& originalPath, const std::string& candidatePath);

    /** Pick the best candidate for one missing sample (no filesystem access). */
    static RelocationMatch chooseBest(const MissingSample& missing,
                                      const std::vector<std::string>& candidates);

    /**
     * Where would `otherOriginal` live, given that `foundOriginal` was relocated to
     * `foundNew`? Walks up both trees in parallel and re-applies the relative
     * structure, then falls back to "same folder as foundNew". Ordered, unique,
     * not checked for existence.
     */
    static std::vector<std::string> predictLocations(const std::string& foundOriginal,
                                                     const std::string& foundNew,
                                                     const std::string& otherOriginal);

    // --- Filesystem-backed search ---

    /**
     * Recursively search `folder` for every missing sample. Ambiguous ties are
     * resolved by sibling consensus when possible; samples not present under
     * `folder` are also tried via the structure of the samples that were found.
     * Result order matches `missing`.
     */
    static std::vector<RelocationMatch> searchFolder(const std::vector<MissingSample>& missing,
                                                     const std::string& folder,
                                                     const RelocatorOptions& options = {});

    /**
     * After one sample was located manually (foundOriginal -> foundNew), try to
     * find `remaining` automatically: first by mirrored folder structure, then by
     * a recursive search of the folder that contains foundNew.
     * Result order matches `remaining`.
     */
    static std::vector<RelocationMatch> cascadeFrom(const std::string& foundOriginal,
                                                    const std::string& foundNew,
                                                    const std::vector<MissingSample>& remaining,
                                                    const RelocatorOptions& options = {});

    static bool fileExists(const std::string& path);
    static bool directoryExists(const std::string& path);
    /** Parent directory of a real path on this machine (UTF-8). */
    static std::string parentOf(const std::string& path);
};

} // namespace looper
