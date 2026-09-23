#include "../Source/PatchStore/PatchStore.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace looper;

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

#define CHECK_NEAR(a, b, eps) do { \
    const double _a = static_cast<double>(a); \
    const double _b = static_cast<double>(b); \
    if (std::fabs(_a - _b) > (eps)) { \
        std::cerr << "FAIL: |" << #a << " - " << #b << "| <= " << (eps) \
                  << " (" << _a << " vs " << _b << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static bool zonesEqual(const Zone& a, const Zone& b)
{
    return a.sampleId == b.sampleId
        && a.rootKey == b.rootKey
        && a.keyLow == b.keyLow
        && a.keyHigh == b.keyHigh
        && a.velLow == b.velLow
        && a.velHigh == b.velHigh
        && a.rrGroup == b.rrGroup
        && a.rrIndex == b.rrIndex
        && std::fabs(a.tuneCents - b.tuneCents) < 1e-5f
        && a.coarseTranspose == b.coarseTranspose
        && std::fabs(a.gainDb - b.gainDb) < 1e-5f
        && std::fabs(a.pan - b.pan) < 1e-5f
        && a.sampleStart == b.sampleStart
        && a.sampleEnd == b.sampleEnd;
}

static bool mapsEqual(const InstrumentMap& a, const InstrumentMap& b)
{
    if (a.zones.size() != b.zones.size()) return false;
    if (std::fabs(a.volumeDb - b.volumeDb) > 1e-5f) return false;
    if (a.polyphonyLimit != b.polyphonyLimit) return false;
    if (std::fabs(a.glideMs - b.glideMs) > 1e-5f) return false;
    if (a.velCurve != b.velCurve) return false;
    if (a.modWheelTarget != b.modWheelTarget) return false;
    for (size_t i = 0; i < a.zones.size(); ++i)
        if (!zonesEqual(a.zones[i], b.zones[i]))
            return false;
    return true;
}

static Patch makeFixturePatch()
{
    Patch p;
    p.schemaVersion = 1;
    p.name = "Piano_Studio_v1";
    p.patchRoot = ".";

    SampleRef s1;
    s1.id = "s_aaa";
    s1.path = "/library/piano/Piano_C4_v32.wav";
    s1.displayName = "Piano_C4_v32.wav";
    s1.durationSamples = 44100;
    s1.sampleRate = 44100.0;
    s1.channels = 2;
    s1.detectedRootKey = 60;

    SampleRef s2;
    s2.id = "s_bbb";
    s2.path = "/library/piano/Piano_C4_v96.wav";
    s2.displayName = "Piano_C4_v96.wav";
    s2.channels = 1;

    SampleRef s3;
    s3.id = "s_ccc";
    s3.path = "/library/piano/snare_rr1.wav";
    s3.displayName = "snare_rr1.wav";

    p.samples = { s1, s2, s3 };

    Zone z1;
    z1.sampleId = "s_aaa";
    z1.rootKey = 60;
    z1.keyLow = 0;
    z1.keyHigh = 64;
    z1.velLow = 1;
    z1.velHigh = 64;
    z1.rrGroup = 0;
    z1.rrIndex = 0;
    z1.tuneCents = -12.5f;
    z1.gainDb = -1.5f;
    z1.pan = -0.25f;
    z1.sampleStart = 10;
    z1.sampleEnd = 40000;

    Zone z2;
    z2.sampleId = "s_bbb";
    z2.rootKey = 60;
    z2.keyLow = 0;
    z2.keyHigh = 64;
    z2.velLow = 65;
    z2.velHigh = 127;

    Zone z3;
    z3.sampleId = "s_ccc";
    z3.rootKey = 38;
    z3.keyLow = 36;
    z3.keyHigh = 40;
    z3.velLow = 1;
    z3.velHigh = 127;
    z3.rrGroup = 1;
    z3.rrIndex = 1;
    z3.coarseTranspose = 12;

    p.map.zones = { z1, z2, z3 };
    p.map.volumeDb = -3.0f;
    p.map.polyphonyLimit = 32;
    p.map.glideMs = 15.0f;
    p.map.velCurve = VelCurve::Soft;
    p.map.modWheelTarget = ModWheelTarget::Volume;
    p.params["attack"] = 2.5;
    p.params["cutoff"] = 8000.0;
    return p;
}

static void testPathHelpers()
{
    std::cout << "testPathHelpers\n";
    CHECK(PatchStore::isAbsolutePath("/foo/bar"));
    CHECK(PatchStore::isAbsolutePath("C:/foo/bar"));
    CHECK(PatchStore::isAbsolutePath("C:\\foo\\bar"));
    CHECK(!PatchStore::isAbsolutePath("rel/path"));
    CHECK(!PatchStore::isAbsolutePath("./x"));

    CHECK_EQ(PatchStore::normalizePath("/a/b/../c/./d"), std::string("/a/c/d"));
    CHECK_EQ(PatchStore::parentDirectory("/proj/patches/Piano.looper.json"),
             std::string("/proj/patches"));

    CHECK_EQ(PatchStore::makeRelativeTo("/proj/patches/samples/a.wav", "/proj/patches"),
             std::string("samples/a.wav"));
    CHECK_EQ(PatchStore::makeRelativeTo("/proj/other/a.wav", "/proj/patches"),
             std::string("../other/a.wav"));
    CHECK_EQ(PatchStore::makeRelativeTo("already/rel.wav", "/proj/patches"),
             std::string("already/rel.wav"));

    CHECK_EQ(PatchStore::resolveAgainst("samples/a.wav", "/proj/patches"),
             std::string("/proj/patches/samples/a.wav"));
    CHECK_EQ(PatchStore::resolveAgainst("../other/a.wav", "/proj/patches"),
             std::string("/proj/other/a.wav"));
    CHECK_EQ(PatchStore::resolveAgainst("/abs/a.wav", "/proj/patches"),
             std::string("/abs/a.wav"));
}

static void testRoundTripJson()
{
    std::cout << "testRoundTripJson\n";
    const auto original = makeFixturePatch();
    const auto json = PatchStore::toJson(original);
    CHECK(json.find("\"schemaVersion\":1") != std::string::npos);
    CHECK(json.find("Piano_Studio_v1") != std::string::npos);
    CHECK(json.find("\"velCurve\":\"soft\"") != std::string::npos);
    CHECK(json.find("\"modWheelTarget\":\"Volume\"") != std::string::npos);

    auto loaded = PatchStore::fromJson(json);
    CHECK(loaded.has_value());
    if (!loaded) return;

    CHECK_EQ(loaded->schemaVersion, 1);
    CHECK_EQ(loaded->name, std::string("Piano_Studio_v1"));
    CHECK_EQ(loaded->samples.size(), original.samples.size());
    CHECK(mapsEqual(loaded->map, original.map));

    for (size_t i = 0; i < original.samples.size(); ++i)
    {
        CHECK_EQ(loaded->samples[i].id, original.samples[i].id);
        CHECK_EQ(loaded->samples[i].path, original.samples[i].path);
        CHECK_EQ(loaded->samples[i].displayName, original.samples[i].displayName);
        CHECK_EQ(loaded->samples[i].channels.value_or(-1),
                 original.samples[i].channels.value_or(-1));
        CHECK_EQ(loaded->samples[i].detectedRootKey.value_or(-1),
                 original.samples[i].detectedRootKey.value_or(-1));
    }

    CHECK_NEAR(loaded->params.at("attack"), 2.5, 1e-9);
    CHECK_NEAR(loaded->params.at("cutoff"), 8000.0, 1e-9);

    // Deterministic: same input → same JSON
    CHECK_EQ(PatchStore::toJson(*loaded), json);
}

static void testRelativePathOnSaveLoad()
{
    std::cout << "testRelativePathOnSaveLoad\n";
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "looper_patch_test";
    fs::create_directories(tmp / "samples");

    // Create dummy sample files so load does not mark them missing
    {
        std::ofstream((tmp / "samples" / "a.wav").string()) << "RIFF";
        std::ofstream((tmp / "samples" / "b.wav").string()) << "RIFF";
    }

    Patch p;
    p.name = "RelTest";
    SampleRef s1;
    s1.id = "id1";
    s1.path = (tmp / "samples" / "a.wav").string();
    s1.displayName = "a.wav";
    SampleRef s2;
    s2.id = "id2";
    s2.path = (tmp / "samples" / "b.wav").string();
    s2.displayName = "b.wav";
    p.samples = { s1, s2 };

    Zone z;
    z.sampleId = "id1";
    z.rootKey = 60;
    z.keyLow = 0;
    z.keyHigh = 127;
    z.velLow = 1;
    z.velHigh = 127;
    p.map.zones.push_back(z);

    const auto patchPath = (tmp / "RelTest.looper.json").string();
    CHECK(PatchStore::save(patchPath, p));

    // On disk, paths should be relative
    {
        std::ifstream ifs(patchPath);
        std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("samples/a.wav") != std::string::npos
              || text.find("samples\\a.wav") != std::string::npos);
        // Should not embed the temp absolute root as the only path form for a.wav
        auto parsed = PatchStore::fromJson(text);
        CHECK(parsed.has_value());
        if (parsed)
        {
            CHECK(!PatchStore::isAbsolutePath(parsed->samples[0].path)
                  || parsed->samples[0].path.find("samples") != std::string::npos);
            // after fromJson alone (no resolve), relative stays relative
            CHECK_EQ(parsed->samples[0].path, std::string("samples/a.wav"));
        }
    }

    auto loaded = PatchStore::load(patchPath);
    CHECK(loaded.has_value());
    if (!loaded) return;
    CHECK(loaded->missingSamplePaths.empty());
    CHECK(loaded->patch.samples.size() == 2);
    // Resolved back to absolute under tmp
    CHECK(PatchStore::isAbsolutePath(loaded->patch.samples[0].path));
    CHECK(loaded->patch.samples[0].path.find("a.wav") != std::string::npos);
    CHECK(mapsEqual(loaded->patch.map, p.map));

    // Missing file reports offline
    fs::remove(tmp / "samples" / "b.wav");
    auto loaded2 = PatchStore::load(patchPath);
    CHECK(loaded2.has_value());
    if (loaded2)
    {
        CHECK_EQ(loaded2->offlineSampleIds.size(), size_t{1});
        CHECK_EQ(loaded2->offlineSampleIds[0], std::string("id2"));
        CHECK(!loaded2->missingSamplePaths.empty());
    }

    fs::remove_all(tmp);
}

static void testBadJson()
{
    std::cout << "testBadJson\n";
    CHECK(!PatchStore::fromJson("").has_value());
    CHECK(!PatchStore::fromJson("{").has_value());
    CHECK(!PatchStore::fromJson("{\"schemaVersion\":99}").has_value());
    // schema 1 with empty body is ok
    auto ok = PatchStore::fromJson("{\"schemaVersion\":1,\"name\":\"x\",\"samples\":[],\"map\":{\"zones\":[]}}");
    CHECK(ok.has_value());
}

int main()
{
    testPathHelpers();
    testRoundTripJson();
    testRelativePathOnSaveLoad();
    testBadJson();

    std::cout << "PatchStoreTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
