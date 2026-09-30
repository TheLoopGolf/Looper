// RelocatorTests — "Relocate missing samples" matching / search logic.
// Uses real temp directories; no JUCE.

#include "../Source/PatchStore/PatchStore.h"
#include "../Source/PatchStore/SampleRelocator.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace looper;
namespace fs = std::filesystem;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    const auto _a = (a); const auto _b = (b); \
    if (!(_a == _b)) { \
        std::cerr << "FAIL: " << #a << " == " << #b << " (" << _a << " vs " << _b \
                  << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

namespace {

fs::path u8(const std::string& s)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string str(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

/** Self-deleting unique temp directory. */
struct TempDir
{
    fs::path root;
    explicit TempDir(const std::string& tag)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("looper_reloc_" + tag + "_" + std::to_string(stamp));
        fs::create_directories(root);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    /** Create file at relative UTF-8 path (forward slashes). Returns its UTF-8 absolute path. */
    std::string touch(const std::string& rel) const
    {
        const auto p = root / u8(rel);
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << "RIFF";
        return str(p);
    }
    std::string path(const std::string& rel = {}) const
    {
        return rel.empty() ? str(root) : str(root / u8(rel));
    }
};

bool sameFile(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    return fs::equivalent(u8(a), u8(b), ec);
}

} // namespace

static void testStringHelpers()
{
    std::cout << "testStringHelpers\n";
    const auto parts = SampleRelocator::splitComponents("C:\\Users\\Zach\\..\\Bob\\./Kit/snare.wav");
    CHECK_EQ(parts.size(), size_t{5});
    if (parts.size() == 5)
    {
        CHECK_EQ(parts[0], std::string("C:"));
        CHECK_EQ(parts[2], std::string("Bob"));
        CHECK_EQ(parts[4], std::string("snare.wav"));
    }
    CHECK_EQ(SampleRelocator::fileNameOf("C:\\a\\b\\Snare 01.WAV"), std::string("Snare 01.WAV"));
    CHECK_EQ(SampleRelocator::fileNameOf("/x/y/z.aif"), std::string("z.aif"));
    CHECK_EQ(SampleRelocator::fileNameOf("\\\\nas\\share\\k.wav"), std::string("k.wav"));
    CHECK_EQ(SampleRelocator::fileNameOf("D:k.wav"), std::string("k.wav"));
    CHECK_EQ(SampleRelocator::foldCase("Piano C4.WAV"), std::string("piano c4.wav"));
    CHECK_EQ(SampleRelocator::foldCase("FL\xC3\x9C" "GEL \xC3\x89t\xC3\xA9"),
             std::string("fl\xC3\xBC" "gel \xC3\xA9t\xC3\xA9")); // FLÜGEL Été -> flügel été
    CHECK(SampleRelocator::equalsIgnoreCase("Snare.Wav", "snare.wav"));
    CHECK(! SampleRelocator::equalsIgnoreCase("snare1.wav", "snare.wav"));

    CHECK_EQ(SampleRelocator::tailMatchScore("/old/Kit/Snares/s1.wav", "/new/Kit/Snares/s1.wav"), 3);
    CHECK_EQ(SampleRelocator::tailMatchScore("C:\\old\\Kit\\SNARES\\S1.wav", "/new/kit/snares/s1.WAV"), 3);
    CHECK_EQ(SampleRelocator::tailMatchScore("/old/a.wav", "/old/b.wav"), 0);
    CHECK_EQ(SampleRelocator::tailMatchScore("/old/x/a.wav", "/new/y/a.wav"), 1);
}

static void testExactMatch()
{
    std::cout << "testExactMatch\n";
    TempDir t("exact");
    const auto disk = t.touch("lib/Piano/C4.wav");
    t.touch("lib/Piano/D4.wav");

    const auto r = SampleRelocator::searchFolder({ { "s1", "/gone/Samples/Piano/C4.wav" } }, t.path("lib"));
    CHECK_EQ(r.size(), size_t{1});
    CHECK(r[0].found());
    CHECK(sameFile(r[0].newPath, disk));
    CHECK_EQ(r[0].sampleId, std::string("s1"));
    CHECK_EQ(r[0].tailScore, 2);
    CHECK_EQ(r[0].candidateCount, 1);
    CHECK(! r[0].ambiguous);
    CHECK(! r[0].viaCascade);
    CHECK(SampleRelocator::fileExists(r[0].newPath));
}

static void testCaseInsensitiveMatch()
{
    std::cout << "testCaseInsensitiveMatch\n";
    TempDir t("case");
    const auto disk = t.touch("lib/piano/c4_soft.WAV");

    const auto r = SampleRelocator::searchFolder({ { "s1", "/Old/PIANO/C4_Soft.wav" } }, t.path("lib"));
    CHECK(r.size() == 1 && r[0].found());
    if (! r.empty())
    {
        CHECK(sameFile(r[0].newPath, disk));
        CHECK_EQ(r[0].tailScore, 2);
        CHECK(! r[0].ambiguous);
    }
}

static void testBestTailMatchAmongDuplicates()
{
    std::cout << "testBestTailMatchAmongDuplicates\n";
    TempDir t("tail");
    t.touch("lib/C4.wav");                   // tail 1
    t.touch("lib/Upright/Soft/C4.wav");      // tail 2
    const auto best = t.touch("lib/Grand/Soft/C4.wav"); // tail 3
    t.touch("lib/Grand/Loud/C4.wav");        // tail 1

    const auto r = SampleRelocator::searchFolder({ { "g", "/Users/me/Libs/Grand/Soft/C4.wav" } }, t.path("lib"));
    CHECK(r.size() == 1 && r[0].found());
    if (! r.empty())
    {
        CHECK(sameFile(r[0].newPath, best));
        CHECK_EQ(r[0].tailScore, 3);
        CHECK_EQ(r[0].candidateCount, 4);
        CHECK(! r[0].ambiguous);
        CHECK(r[0].alternatives.empty());
    }
}

static void testAmbiguityFlag()
{
    std::cout << "testAmbiguityFlag\n";
    // Pure: two candidates with equal tail score.
    {
        const auto m = SampleRelocator::chooseBest({ "x", "/old/Drums/snare.wav" },
                                                   { "/lib/KitB/snare.wav", "/lib/KitA/snare.wav", "/lib/KitA/kick.wav" });
        CHECK(m.found());
        CHECK(m.ambiguous);
        CHECK_EQ(m.candidateCount, 2);
        CHECK_EQ(m.newPath, std::string("/lib/KitA/snare.wav")); // deterministic tie-break
        CHECK_EQ(m.alternatives.size(), size_t{1});
        CHECK_EQ(m.tailScore, 1);
    }
    // Pure: exact-case file name wins the tie but stays flagged.
    {
        const auto m = SampleRelocator::chooseBest({ "x", "/old/Snare.wav" },
                                                   { "/lib/a/snare.wav", "/lib/b/Snare.wav" });
        CHECK(m.ambiguous);
        CHECK_EQ(m.newPath, std::string("/lib/b/Snare.wav"));
    }
    // Filesystem
    TempDir t("ambig");
    const auto a = t.touch("lib/KitA/snare.wav");
    const auto b = t.touch("lib/KitB/snare.wav");
    const auto r = SampleRelocator::searchFolder({ { "sn", "/old/Drums/snare.wav" } }, t.path("lib"));
    CHECK(r.size() == 1 && r[0].found());
    if (! r.empty())
    {
        CHECK(r[0].ambiguous);
        CHECK_EQ(r[0].candidateCount, 2);
        CHECK_EQ(r[0].alternatives.size(), size_t{1});
        CHECK(sameFile(r[0].newPath, a) || sameFile(r[0].newPath, b));
    }
}

static void testSiblingConsensusResolvesAmbiguity()
{
    std::cout << "testSiblingConsensusResolvesAmbiguity\n";
    TempDir t("consensus");
    t.touch("lib/KitA/snare.wav");
    const auto snareB = t.touch("lib/KitB/snare.wav");
    const auto kickB = t.touch("lib/KitB/kick.wav"); // unique -> anchor

    const auto r = SampleRelocator::searchFolder({ { "sn", "/old/Drums/snare.wav" },
                                                   { "kk", "/old/Drums/kick.wav" } },
                                                 t.path("lib"));
    CHECK_EQ(r.size(), size_t{2});
    if (r.size() == 2)
    {
        CHECK(sameFile(r[1].newPath, kickB));
        CHECK(! r[1].ambiguous);
        CHECK(sameFile(r[0].newPath, snareB));
        CHECK(! r[0].ambiguous);
        CHECK(r[0].viaCascade);
    }
}

static void testCascadeFromOneFound()
{
    std::cout << "testCascadeFromOneFound\n";
    TempDir t("cascade");
    const auto s1 = t.touch("new/Kit Renamed/Snares/s1.wav");
    const auto k1 = t.touch("new/Kit Renamed/Kicks/k1.wav");
    const auto root = t.touch("new/Kit Renamed/root.wav");
    const auto deep = t.touch("new/Kit Renamed/Snares/extra/deep.wav"); // only via recursive fallback

    const std::vector<MissingSample> rest {
        { "k1", "/old/Kit/Kicks/k1.wav" },
        { "root", "/old/Kit/root.wav" },
        { "deep", "/old/Somewhere/Else/deep.wav" },
        { "gone", "/old/Kit/Toms/gone.wav" },
    };
    const auto r = SampleRelocator::cascadeFrom("/old/Kit/Snares/s1.wav", s1, rest);
    CHECK_EQ(r.size(), size_t{4});
    if (r.size() == 4)
    {
        CHECK(sameFile(r[0].newPath, k1));
        CHECK(r[0].viaCascade);
        CHECK_EQ(r[0].tailScore, 2);
        CHECK(sameFile(r[1].newPath, root));
        CHECK(r[1].viaCascade);
        CHECK(sameFile(r[2].newPath, deep));
        CHECK(r[2].viaCascade);
        CHECK(! r[3].found());
        CHECK_EQ(r[3].sampleId, std::string("gone"));
    }

    // Pure prediction: sibling folder structure is mirrored.
    const auto preds = SampleRelocator::predictLocations("/old/Kit/Snares/s1.wav", s1, "/old/Kit/Kicks/k1.wav");
    CHECK(! preds.empty());
    if (! preds.empty())
        CHECK(sameFile(preds.front(), k1));

    // Search a sub-folder only: siblings outside it are found via structure.
    TempDir t2("subfolder");
    const auto sn = t2.touch("lib/Kit/Snares/sn.wav");
    const auto kk = t2.touch("lib/Kit/Kicks/kk.wav");
    const auto r2 = SampleRelocator::searchFolder({ { "sn", "/old/Kit/Snares/sn.wav" },
                                                    { "kk", "/old/Kit/Kicks/kk.wav" } },
                                                  t2.path("lib/Kit/Snares"));
    CHECK(r2.size() == 2 && sameFile(r2[0].newPath, sn) && sameFile(r2[1].newPath, kk));
    if (r2.size() == 2)
    {
        CHECK(! r2[0].viaCascade);
        CHECK(r2[1].viaCascade);
    }
}

static void testNothingFound()
{
    std::cout << "testNothingFound\n";
    TempDir t("none");
    t.touch("lib/unrelated.wav");
    const std::vector<MissingSample> missing { { "a", "/old/a.wav" }, { "b", "C:\\old\\b.wav" } };

    const auto r = SampleRelocator::searchFolder(missing, t.path("lib"));
    CHECK_EQ(r.size(), size_t{2});
    for (const auto& m : r)
    {
        CHECK(! m.found());
        CHECK_EQ(m.candidateCount, 0);
        CHECK(! m.ambiguous);
    }
    const auto r2 = SampleRelocator::searchFolder(missing, t.path("does/not/exist"));
    CHECK(r2.size() == 2 && ! r2[0].found() && ! r2[1].found());
    CHECK_EQ(r2[0].sampleId, std::string("a"));

    const auto r3 = SampleRelocator::searchFolder({}, t.path("lib"));
    CHECK(r3.empty());

    FolderIndex idx;
    CHECK(! idx.build(t.path("nope")));
    CHECK(idx.build(t.path("lib")));
    CHECK_EQ(idx.fileCount(), size_t{1});
    CHECK(! idx.truncated());
    CHECK(idx.candidatesFor("UNRELATED.WAV") != nullptr);
    CHECK(idx.candidatesFor("missing.wav") == nullptr);

    RelocatorOptions tiny;
    tiny.maxFilesScanned = 1;
    t.touch("lib/more/x.wav");
    CHECK(idx.build(t.path("lib"), tiny));
    CHECK(idx.truncated());
}

static void testWindowsStylePaths()
{
    std::cout << "testWindowsStylePaths\n";
    TempDir t("win");
    const auto grand = t.touch("lib/Grand Piano/Soft/C4.wav");
    t.touch("lib/Upright/Soft/C4.wav");
    const auto snare = t.touch("lib/Kit/snare.wav");
    const auto k1 = t.touch("moved/Kit2/Kicks/k1.wav");
    const auto s1 = t.touch("moved/Kit2/Snares/s1.wav");

    const auto r = SampleRelocator::searchFolder({
        { "g", "C:\\Users\\Zach\\Samples\\Grand Piano\\Soft\\C4.wav" },
        { "u", "\\\\nas\\share\\Kit\\SNARE.wav" },
        { "d", "d:/Mixed\\Seps/Kit/snare.wav" },
    }, t.path("lib"));
    CHECK_EQ(r.size(), size_t{3});
    if (r.size() == 3)
    {
        CHECK(sameFile(r[0].newPath, grand));
        CHECK_EQ(r[0].tailScore, 3);
        CHECK(! r[0].ambiguous);
        CHECK(sameFile(r[1].newPath, snare));
        CHECK_EQ(r[1].tailScore, 2);
        CHECK(sameFile(r[2].newPath, snare));
    }

    const auto c = SampleRelocator::cascadeFrom("C:\\Old\\Kit\\Snares\\s1.wav", s1,
                                                { { "k1", "C:\\Old\\Kit\\Kicks\\k1.wav" } });
    CHECK(c.size() == 1 && sameFile(c[0].newPath, k1));

    // PatchStore still resolves a relative path against a drive-letter base without duplication.
    CHECK_EQ(PatchStore::resolveAgainst("Kit/k1.wav", "C:\\Patches"), std::string("C:/Patches/Kit/k1.wav"));
}

static void testSpacesAndUnicode()
{
    std::cout << "testSpacesAndUnicode\n";
    TempDir t("unicode");
    // "Café Keys/Flügel Soft/Flügel C4 — soft.wav"
    const auto fl = t.touch("lib/Caf\xC3\xA9 Keys/Fl\xC3\xBCgel Soft/Fl\xC3\xBCgel C4 \xE2\x80\x94 soft.wav");
    // "日本語/ピアノ C4.wav"
    const auto jp = t.touch("lib/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E/\xE3\x83\x94\xE3\x82\xA2\xE3\x83\x8E C4.wav");

    const auto r = SampleRelocator::searchFolder({
        // Upper-cased Latin-1 letters in the original must still match
        { "fl", "/Users/zach/CAF\xC3\x89 KEYS/FL\xC3\x9CGEL SOFT/FL\xC3\x9CGEL c4 \xE2\x80\x94 SOFT.WAV" },
        { "jp", "E:\\Samples\\\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\\\xE3\x83\x94\xE3\x82\xA2\xE3\x83\x8E C4.wav" },
    }, t.path("lib"));
    CHECK_EQ(r.size(), size_t{2});
    if (r.size() == 2)
    {
        CHECK(sameFile(r[0].newPath, fl));
        CHECK_EQ(r[0].tailScore, 3);
        CHECK(sameFile(r[1].newPath, jp));
        CHECK_EQ(r[1].tailScore, 2);
        CHECK(SampleRelocator::fileExists(r[1].newPath));
    }
}

static void testPatchRoundTripAfterRelocation()
{
    std::cout << "testPatchRoundTripAfterRelocation\n";
    TempDir t("patch");
    const auto sample = t.touch("samples/Piano/C4.wav");
    fs::create_directories(t.root / "patches");
    const auto patchPath = t.path("patches/p.looper.json");

    Patch p;
    p.name = "Relocate me";
    SampleRef ref;
    ref.id = "c4";
    ref.path = sample;
    ref.displayName = "C4.wav";
    p.samples.push_back(ref);
    Zone z;
    z.sampleId = "c4";
    p.map.zones.push_back(z);
    CHECK(PatchStore::save(patchPath, p));

    // Move the library: the patch now points at a missing file but still loads.
    fs::create_directories(t.root / "moved");
    fs::rename(t.root / "samples", t.root / "moved" / "My Library");
    auto loaded = PatchStore::load(patchPath);
    CHECK(loaded.has_value());
    if (! loaded) return;
    CHECK_EQ(loaded->offlineSampleIds.size(), size_t{1});
    CHECK_EQ(loaded->patch.map.zones.size(), size_t{1});

    std::vector<MissingSample> missing;
    for (const auto& s : loaded->patch.samples)
        missing.push_back({ s.id, s.path });
    const auto r = SampleRelocator::searchFolder(missing, t.path("moved"));
    CHECK(r.size() == 1 && r[0].found());
    if (r.empty() || ! r[0].found()) return;
    loaded->patch.samples[0].path = r[0].newPath;
    CHECK(PatchStore::save(patchPath, loaded->patch));

    std::ifstream ifs(u8(patchPath), std::ios::binary);
    std::stringstream ss;
    ss << ifs.rdbuf();
    CHECK(ss.str().find("../moved/My Library/Piano/C4.wav") != std::string::npos);

    auto again = PatchStore::load(patchPath);
    CHECK(again.has_value() && again->offlineSampleIds.empty());
}

int main()
{
    testStringHelpers();
    testExactMatch();
    testCaseInsensitiveMatch();
    testBestTailMatchAmongDuplicates();
    testAmbiguityFlag();
    testSiblingConsensusResolvesAmbiguity();
    testCascadeFromOneFound();
    testNothingFound();
    testWindowsStylePaths();
    testSpacesAndUnicode();
    testPatchRoundTripAfterRelocation();

    std::cout << "RelocatorTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
