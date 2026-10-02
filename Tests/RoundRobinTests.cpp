// Round-robin mode tests: Cycle (v1, unchanged) and Random (no back-to-back repeats),
// real-time safety (no allocation in zone selection), seed determinism, patch round trip.

#include "../Source/PatchStore/PatchStore.h"
#include "../Source/SamplePool/SamplePool.h"
#include "../Source/VoiceEngine/FastRng.h"
#include "../Source/VoiceEngine/VoiceEngine.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <vector>

using namespace looper;

// --- Global allocation counter (proves selectZone / noteOn never allocate) ---------------
static std::atomic<long> g_allocs { 0 };
void* operator new(std::size_t n)
{
    ++g_allocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    ++g_allocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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

static Zone makeZone(const std::string& id, int keyLo, int keyHi, int velLo, int velHi,
                     int rrGroup, int rrIndex, int root = 60)
{
    Zone z;
    z.sampleId = id;
    z.rootKey = root;
    z.keyLow = keyLo;
    z.keyHigh = keyHi;
    z.velLow = velLo;
    z.velHigh = velHi;
    z.rrGroup = rrGroup;
    z.rrIndex = rrIndex;
    return z;
}

/** Four alternates of C4 (group 1), deliberately out of rrIndex order in the map. */
static InstrumentMap fourAlternates()
{
    InstrumentMap map;
    map.zones.push_back(makeZone("rr2", 48, 72, 1, 127, 1, 2));
    map.zones.push_back(makeZone("rr0", 48, 72, 1, 127, 1, 0));
    map.zones.push_back(makeZone("rr3", 48, 72, 1, 127, 1, 3));
    map.zones.push_back(makeZone("rr1", 48, 72, 1, 127, 1, 1));
    return map;
}

static std::string pick(VoiceEngine& e, int note = 60, int vel = 100)
{
    const Zone* z = e.selectZone(note, vel);
    return z != nullptr ? z->sampleId : std::string("<none>");
}

// ---------------------------------------------------------------------------------------

static void testDefaultModeIsCycle()
{
    std::cout << "testDefaultModeIsCycle\n";
    VoiceEngine e;
    CHECK(e.roundRobinMode() == RoundRobinMode::Cycle);
    InstrumentMap map;
    CHECK(map.rrMode == RoundRobinMode::Cycle);
    Patch p;
    CHECK(p.map.rrMode == RoundRobinMode::Cycle);
}

static void testCycleUnchanged()
{
    std::cout << "testCycleUnchanged\n";
    const auto map = fourAlternates();

    // Default mode and explicit Cycle give the exact v1 sequence (rrIndex order, wrap).
    for (int explicitMode = 0; explicitMode < 2; ++explicitMode)
    {
        VoiceEngine e;
        if (explicitMode)
            e.setRoundRobinMode(RoundRobinMode::Cycle);
        e.setMap(&map);
        const char* expected[] = { "rr0", "rr1", "rr2", "rr3", "rr0", "rr1", "rr2", "rr3", "rr0" };
        for (const char* id : expected)
            CHECK_EQ(pick(e), std::string(id));
    }

    // Seeding the RNG has no effect on Cycle.
    {
        VoiceEngine a, b;
        a.setRandomSeed(1);
        b.setRandomSeed(999);
        a.setMap(&map);
        b.setMap(&map);
        for (int i = 0; i < 12; ++i)
            CHECK_EQ(pick(a), pick(b));
    }

    // Counter is per rrGroup (v1): two groups on different keys cycle independently.
    {
        InstrumentMap two;
        two.zones.push_back(makeZone("a0", 60, 60, 1, 127, 1, 0));
        two.zones.push_back(makeZone("a1", 60, 60, 1, 127, 1, 1));
        two.zones.push_back(makeZone("b0", 62, 62, 1, 127, 2, 0));
        two.zones.push_back(makeZone("b1", 62, 62, 1, 127, 2, 1));
        two.zones.push_back(makeZone("b2", 62, 62, 1, 127, 2, 2));
        VoiceEngine e;
        e.setMap(&two);
        CHECK_EQ(pick(e, 60), std::string("a0"));
        CHECK_EQ(pick(e, 62), std::string("b0"));
        CHECK_EQ(pick(e, 62), std::string("b1"));
        CHECK_EQ(pick(e, 60), std::string("a1"));
        CHECK_EQ(pick(e, 62), std::string("b2"));
        CHECK_EQ(pick(e, 60), std::string("a0"));
        CHECK_EQ(pick(e, 62), std::string("b0"));
    }

    // clearRrCounters / setMap restart the cycle.
    {
        VoiceEngine e;
        e.setMap(&map);
        (void) pick(e);
        (void) pick(e);
        e.mapChanged();
        CHECK_EQ(pick(e), std::string("rr0"));
    }
}

static void testRandomNeverRepeatsBackToBack()
{
    std::cout << "testRandomNeverRepeatsBackToBack\n";
    const auto map = fourAlternates();
    VoiceEngine e;
    e.setRandomSeed(12345);
    e.setRoundRobinMode(RoundRobinMode::Random);
    e.setMap(&map);

    std::string prev = pick(e);
    int repeats = 0;
    for (int i = 0; i < 20000; ++i)
    {
        const auto cur = pick(e);
        if (cur == prev)
            ++repeats;
        prev = cur;
    }
    CHECK_EQ(repeats, 0);

    // Two alternates: Random must strictly alternate.
    InstrumentMap two;
    two.zones.push_back(makeZone("x", 60, 60, 1, 127, 7, 1));
    two.zones.push_back(makeZone("y", 60, 60, 1, 127, 7, 2));
    VoiceEngine e2;
    e2.setRandomSeed(42);
    e2.setRoundRobinMode(RoundRobinMode::Random);
    e2.setMap(&two);
    std::string last = pick(e2);
    bool alternates = true;
    for (int i = 0; i < 1000; ++i)
    {
        const auto cur = pick(e2);
        alternates = alternates && cur != last;
        last = cur;
    }
    CHECK(alternates);
}

static void testRandomUsesAllUniformly()
{
    std::cout << "testRandomUsesAllUniformly\n";
    const auto map = fourAlternates();
    constexpr int kTrials = 40000;

    for (uint64_t seed : { 1ull, 7ull, 0xC0FFEEull })
    {
        VoiceEngine e;
        e.setRandomSeed(seed);
        e.setRoundRobinMode(RoundRobinMode::Random);
        e.setMap(&map);

        std::map<std::string, int> counts;
        std::map<std::string, std::map<std::string, int>> transitions;
        std::string prev = pick(e);
        for (int i = 0; i < kTrials; ++i)
        {
            const auto cur = pick(e);
            ++counts[cur];
            ++transitions[prev][cur];
            prev = cur;
        }

        CHECK_EQ(counts.size(), size_t{4}); // every alternate used
        // Chi-square vs uniform, df = 3: 16.27 is the p = 0.001 critical value.
        const double expected = kTrials / 4.0;
        double chi2 = 0.0;
        for (const auto& [id, n] : counts)
        {
            chi2 += (n - expected) * (n - expected) / expected;
            CHECK(std::fabs(n - expected) < expected * 0.05); // within 5% of 10000
        }
        CHECK(chi2 < 16.27);

        // From each alternate, the next one is uniform over the other three (~1/3 each).
        for (const auto& [from, row] : transitions)
        {
            CHECK_EQ(row.size(), size_t{3});
            CHECK(row.find(from) == row.end());
            int total = 0;
            for (const auto& kv : row)
                total += kv.second;
            for (const auto& kv : row)
            {
                const double share = static_cast<double>(kv.second) / static_cast<double>(total);
                CHECK(std::fabs(share - 1.0 / 3.0) < 0.04);
            }
        }
    }
}

static void testSingleAlternate()
{
    std::cout << "testSingleAlternate\n";
    // One zone in a non-zero rrGroup: just play it, in both modes.
    InstrumentMap one;
    one.zones.push_back(makeZone("solo", 0, 127, 1, 127, 3, 1));
    for (auto mode : { RoundRobinMode::Cycle, RoundRobinMode::Random })
    {
        VoiceEngine e;
        e.setRoundRobinMode(mode);
        e.setMap(&one);
        bool allSolo = true;
        for (int i = 0; i < 50; ++i)
            allSolo = allSolo && pick(e) == "solo";
        CHECK(allSolo);
    }

    // rrGroup 0 (no RR) duplicates: first stable match always wins, even in Random.
    InstrumentMap dup;
    dup.zones.push_back(makeZone("first", 0, 127, 1, 127, 0, 0));
    dup.zones.push_back(makeZone("second", 0, 127, 1, 127, 0, 1));
    VoiceEngine e;
    e.setRoundRobinMode(RoundRobinMode::Random);
    e.setMap(&dup);
    bool allFirst = true;
    for (int i = 0; i < 50; ++i)
        allFirst = allFirst && pick(e) == "first";
    CHECK(allFirst);

    // No match still returns nullptr.
    InstrumentMap narrow;
    narrow.zones.push_back(makeZone("c4", 60, 60, 1, 127, 1, 1));
    narrow.zones.push_back(makeZone("c4b", 60, 60, 1, 127, 1, 2));
    VoiceEngine e2;
    e2.setRoundRobinMode(RoundRobinMode::Random);
    e2.setMap(&narrow);
    CHECK(e2.selectZone(61, 100) == nullptr);
}

static void testSeedDeterminism()
{
    std::cout << "testSeedDeterminism\n";
    const auto map = fourAlternates();
    auto run = [&map](uint64_t seed) {
        VoiceEngine e;
        e.setRandomSeed(seed);
        e.setRoundRobinMode(RoundRobinMode::Random);
        e.setMap(&map);
        std::vector<std::string> seq;
        for (int i = 0; i < 500; ++i)
            seq.push_back(pick(e));
        return seq;
    };
    const auto a1 = run(2026);
    const auto a2 = run(2026);
    const auto b = run(2027);
    CHECK(a1 == a2);
    CHECK(a1 != b);

    // Re-seeding the same engine replays the sequence (after clearing RR memory).
    VoiceEngine e;
    e.setRoundRobinMode(RoundRobinMode::Random);
    e.setMap(&map);
    e.setRandomSeed(2026);
    std::vector<std::string> first;
    for (int i = 0; i < 100; ++i)
        first.push_back(pick(e));
    e.setRandomSeed(2026);
    e.clearRrCounters();
    std::vector<std::string> second;
    for (int i = 0; i < 100; ++i)
        second.push_back(pick(e));
    CHECK(first == second);

    // FastRng: bounded output, deterministic.
    FastRng r1(5), r2(5);
    bool same = true, inRange = true;
    for (int i = 0; i < 1000; ++i)
    {
        const auto x = r1.nextBelow(7);
        same = same && x == r2.nextBelow(7);
        inRange = inRange && x < 7u;
    }
    CHECK(same);
    CHECK(inRange);
}

static void testPerGroupState()
{
    std::cout << "testPerGroupState\n";
    // Two velocity layers x 3 alternates (AutoMapper style: one rrGroup per layer) plus a
    // hand-made patch that reuses rrGroup 9 on two different keys. Interleaved playing must
    // never repeat back-to-back *within* each keyzone / velocity-layer group.
    InstrumentMap map;
    for (int i = 0; i < 3; ++i)
        map.zones.push_back(makeZone("soft" + std::to_string(i), 60, 60, 1, 64, 1, i + 1));
    for (int i = 0; i < 3; ++i)
        map.zones.push_back(makeZone("hard" + std::to_string(i), 60, 60, 65, 127, 2, i + 1));
    for (int i = 0; i < 3; ++i)
        map.zones.push_back(makeZone("d" + std::to_string(i), 62, 62, 1, 127, 9, i + 1, 62));
    for (int i = 0; i < 3; ++i)
        map.zones.push_back(makeZone("e" + std::to_string(i), 64, 64, 1, 127, 9, i + 1, 64));

    VoiceEngine e;
    e.setRandomSeed(77);
    e.setRoundRobinMode(RoundRobinMode::Random);
    e.setMap(&map);

    struct Hit { int note; int vel; char prefix; };
    const Hit pattern[] = { { 60, 40, 's' }, { 60, 110, 'h' }, { 62, 100, 'd' }, { 64, 100, 'e' } };
    std::map<char, std::string> last;
    std::map<char, std::map<std::string, int>> used;
    int repeats = 0, wrongLayer = 0;
    FastRng order(3);
    for (int i = 0; i < 8000; ++i)
    {
        const auto& h = pattern[order.nextBelow(4)];
        const auto id = pick(e, h.note, h.vel);
        if (id.empty() || id[0] != h.prefix)
            ++wrongLayer;
        if (last.count(h.prefix) && last[h.prefix] == id)
            ++repeats;
        last[h.prefix] = id;
        ++used[h.prefix][id];
    }
    CHECK_EQ(repeats, 0);
    CHECK_EQ(wrongLayer, 0);
    for (const auto& [prefix, ids] : used)
        CHECK_EQ(ids.size(), size_t{3});
}

static void testModeSwitchAvoidsRepeat()
{
    std::cout << "testModeSwitchAvoidsRepeat\n";
    const auto map = fourAlternates();
    for (uint64_t seed = 1; seed <= 200; ++seed)
    {
        VoiceEngine e;
        e.setRandomSeed(seed);
        e.setMap(&map);
        (void) pick(e);                  // rr0
        const auto lastCycle = pick(e);  // rr1
        e.setRoundRobinMode(RoundRobinMode::Random);
        if (pick(e) == lastCycle)
        {
            CHECK(false && "first Random pick repeated the last Cycle pick");
            return;
        }
    }
    CHECK(true);

    // Back to Cycle: the cycle counter resumes where it was (Random does not advance it).
    VoiceEngine e;
    e.setMap(&map);
    CHECK_EQ(pick(e), std::string("rr0"));
    e.setRoundRobinMode(RoundRobinMode::Random);
    for (int i = 0; i < 5; ++i)
        (void) pick(e);
    e.setRoundRobinMode(RoundRobinMode::Cycle);
    CHECK_EQ(pick(e), std::string("rr1"));
}

static void testNoAllocationInSelection()
{
    std::cout << "testNoAllocationInSelection\n";
    const auto map = fourAlternates();
    SamplePool pool;
    for (const char* id : { "rr0", "rr1", "rr2", "rr3" })
        pool.setBuffer(id, makeDemoToneBuffer(44100.0, 0.05));

    VoiceEngine e;
    e.setSamplePool(&pool);
    e.setSampleRate(44100.0);
    e.setMap(&map);
    std::vector<float> l(64), r(64);

    for (auto mode : { RoundRobinMode::Cycle, RoundRobinMode::Random })
    {
        e.setRoundRobinMode(mode);
        // warm-up (first touch of each code path)
        e.noteOn(60, 100, 1);
        e.noteOff(60, 1);
        const long before = g_allocs.load();
        for (int i = 0; i < 5000; ++i)
        {
            (void) e.selectZone(60, 100);
            e.noteOn(60 + (i % 3), 100, 1);
            e.noteOff(60 + (i % 3), 1);
            if ((i & 63) == 0)
                e.processBlock(l.data(), r.data(), 64);
        }
        CHECK_EQ(g_allocs.load() - before, 0L);
    }
}

static void testNoteOnPlaysSelectedAlternate()
{
    std::cout << "testNoteOnPlaysSelectedAlternate\n";
    const auto map = fourAlternates();
    SamplePool pool;
    for (const char* id : { "rr0", "rr1", "rr2", "rr3" })
        pool.setBuffer(id, makeDemoToneBuffer(44100.0, 0.2));

    VoiceEngine e;
    e.setSamplePool(&pool);
    e.setSampleRate(44100.0);
    e.setMap(&map);

    const char* expected[] = { "rr0", "rr1", "rr2", "rr3", "rr0" };
    for (const char* id : expected)
    {
        e.noteOn(60, 100, 1);
        const Voice* v = e.findActiveVoice(60, 1);
        CHECK(v != nullptr && v->zone.sampleId == id);
        e.allNotesOff();
    }

    e.setRoundRobinMode(RoundRobinMode::Random);
    std::string prev;
    int repeats = 0;
    for (int i = 0; i < 200; ++i)
    {
        e.noteOn(60, 100, 1);
        const Voice* v = e.findActiveVoice(60, 1);
        const std::string id = v != nullptr ? v->zone.sampleId : std::string();
        if (id == prev)
            ++repeats;
        prev = id;
        e.allNotesOff();
    }
    CHECK_EQ(repeats, 0);
}

static void testPatchRoundTrip()
{
    std::cout << "testPatchRoundTrip\n";
    Patch p;
    p.name = "RR";
    p.map = fourAlternates();

    // Random round-trips through JSON
    p.map.rrMode = RoundRobinMode::Random;
    const auto json = PatchStore::toJson(p);
    CHECK(json.find("\"roundRobinMode\":\"random\"") != std::string::npos);
    auto loaded = PatchStore::fromJson(json);
    CHECK(loaded.has_value());
    if (loaded)
    {
        CHECK(loaded->map.rrMode == RoundRobinMode::Random);
        CHECK_EQ(PatchStore::toJson(*loaded), json);
    }

    // Cycle is written explicitly too
    p.map.rrMode = RoundRobinMode::Cycle;
    const auto jsonCycle = PatchStore::toJson(p);
    CHECK(jsonCycle.find("\"roundRobinMode\":\"cycle\"") != std::string::npos);
    auto loadedCycle = PatchStore::fromJson(jsonCycle);
    CHECK(loadedCycle.has_value() && loadedCycle->map.rrMode == RoundRobinMode::Cycle);

    // Save/load through a real .looper.json file
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "looper_rr_test";
    fs::create_directories(dir);
    const auto file = (dir / "rr.looper.json").string();
    p.map.rrMode = RoundRobinMode::Random;
    CHECK(PatchStore::save(file, p));
    auto fromDisk = PatchStore::load(file);
    CHECK(fromDisk.has_value() && fromDisk->patch.map.rrMode == RoundRobinMode::Random);
    fs::remove_all(dir);

    // String helpers
    CHECK_EQ(PatchStore::roundRobinModeToString(RoundRobinMode::Random), std::string("random"));
    CHECK_EQ(PatchStore::roundRobinModeToString(RoundRobinMode::Cycle), std::string("cycle"));
    CHECK(PatchStore::roundRobinModeFromString("Random") == RoundRobinMode::Random);
    CHECK(!PatchStore::roundRobinModeFromString("shuffle").has_value());
}

static void testOldPatchDefaultsToCycle()
{
    std::cout << "testOldPatchDefaultsToCycle\n";
    // A patch written before the field existed (schema v1, no "roundRobinMode")
    const std::string oldJson =
        "{\"schemaVersion\":1,\"name\":\"Old\",\"patchRoot\":\".\",\"samples\":[],"
        "\"map\":{\"volumeDb\":0,\"polyphonyLimit\":64,\"glideMs\":0,\"velCurve\":\"linear\","
        "\"modWheelTarget\":\"FilterCutoff\",\"zones\":[{\"sampleId\":\"a\",\"rootKey\":60,"
        "\"keyLow\":0,\"keyHigh\":127,\"velLow\":1,\"velHigh\":127,\"rrGroup\":1,\"rrIndex\":1}]},"
        "\"params\":{}}";
    auto old = PatchStore::fromJson(oldJson);
    CHECK(old.has_value());
    if (old)
    {
        CHECK(old->map.rrMode == RoundRobinMode::Cycle);
        CHECK_EQ(old->map.zones.size(), size_t{1});
    }

    // Field stripped from a Random patch -> Cycle; unknown value -> Cycle
    Patch p;
    p.map.rrMode = RoundRobinMode::Random;
    auto json = PatchStore::toJson(p);
    const std::string field = "\"roundRobinMode\":\"random\",";
    const auto at = json.find(field);
    CHECK(at != std::string::npos);
    if (at != std::string::npos)
    {
        auto stripped = json;
        stripped.erase(at, field.size());
        auto s = PatchStore::fromJson(stripped);
        CHECK(s.has_value() && s->map.rrMode == RoundRobinMode::Cycle);

        auto unknown = json;
        unknown.replace(at, field.size(), "\"roundRobinMode\":\"shuffle\",");
        auto u = PatchStore::fromJson(unknown);
        CHECK(u.has_value() && u->map.rrMode == RoundRobinMode::Cycle);
    }
}

int main()
{
    testDefaultModeIsCycle();
    testCycleUnchanged();
    testRandomNeverRepeatsBackToBack();
    testRandomUsesAllUniformly();
    testSingleAlternate();
    testSeedDeterminism();
    testPerGroupState();
    testModeSwitchAvoidsRepeat();
    testNoAllocationInSelection();
    testNoteOnPlaysSelectedAlternate();
    testPatchRoundTrip();
    testOldPatchDefaultsToCycle();

    std::cout << "RoundRobinTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
