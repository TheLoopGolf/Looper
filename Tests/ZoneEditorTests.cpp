// Zone editor tests: field edits + clamping, inverted-range repair, overlap warnings,
// "Reset to auto" / "Use detected pitch", undoable edit records, .looper.json round trip,
// live voice parameter updates (real-time hand-off, no audio-thread allocation), audition
// note choice and, when built with JUCE, the real juce::UndoManager + ZoneEditAction path.

#include "../Source/PatchStore/PatchStore.h"
#include "../Source/SamplePool/SamplePool.h"
#include "../Source/VoiceEngine/VoiceEngine.h"
#include "../Source/ZoneEdit/ZoneEditor.h"

#if LOOPER_ZONE_EDIT_WITH_JUCE
 #include "../Source/ZoneEdit/ZoneEditAction.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace looper;

#if defined(__GNUC__) && !defined(__clang__)
 // GCC false positive for the counting operator new/delete below (same as RoundRobinTests)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// --- Global allocation counter (audio-thread hand-off must not allocate or free) ----------
static std::atomic<long> g_allocs { 0 };
static std::atomic<long> g_frees { 0 };
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
void operator delete(void* p) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete[](void* p) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) ++g_frees; std::free(p); }

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
    const double _a = static_cast<double>(a); const double _b = static_cast<double>(b); \
    if (!(std::fabs(_a - _b) <= (eps))) { \
        std::cerr << "FAIL: " << #a << " ~= " << #b << " (" << _a << " vs " << _b \
                  << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static Zone makeZone(const std::string& id, int keyLo, int keyHi, int root,
                     int velLo = 1, int velHi = 127, int rrGroup = 0, int rrIndex = 0)
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

static SampleBuffer sineBuffer(double hz = 220.0, double seconds = 2.0, double sr = 44100.0)
{
    SampleBuffer b;
    b.channels = 1;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(seconds * sr);
    b.interleaved.resize(static_cast<size_t>(b.length));
    for (int64_t i = 0; i < b.length; ++i)
        b.interleaved[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(6.283185307179586 * hz * static_cast<double>(i) / sr));
    return b;
}

static double ratioForSemis(double semis) { return std::pow(2.0, semis / 12.0); }

// ---------------------------------------------------------------------------------------

static void testFieldEditsAndClamping()
{
    std::cout << "testFieldEditsAndClamping\n";
    const Zone base = makeZone("a", 48, 72, 60, 1, 127);

    CHECK_EQ(withZoneField(base, ZoneField::RootKey, 64).rootKey, 64);
    CHECK_EQ(withZoneField(base, ZoneField::RootKey, 60.6).rootKey, 61);   // rounds
    CHECK_EQ(withZoneField(base, ZoneField::RootKey, 200).rootKey, 127);   // clamp high
    CHECK_EQ(withZoneField(base, ZoneField::RootKey, -12).rootKey, 0);     // clamp low
    CHECK_EQ(withZoneField(base, ZoneField::RootKey, std::numeric_limits<double>::quiet_NaN()).rootKey, 0);

    CHECK_EQ(withZoneField(base, ZoneField::KeyLow, 50).keyLow, 50);
    CHECK_EQ(withZoneField(base, ZoneField::KeyHigh, 130).keyHigh, 127);
    CHECK_EQ(withZoneField(base, ZoneField::KeyLow, -1).keyLow, 0);

    CHECK_EQ(withZoneField(base, ZoneField::VelLow, 0).velLow, 1);         // 0 is note-off
    CHECK_EQ(withZoneField(base, ZoneField::VelHigh, 300).velHigh, 127);

    CHECK_NEAR(withZoneField(base, ZoneField::TuneCents, -15.0).tuneCents, -15.0, 1e-6);
    CHECK_NEAR(withZoneField(base, ZoneField::TuneCents, 12.345).tuneCents, 12.35, 1e-4); // 0.01 ct grid
    CHECK_NEAR(withZoneField(base, ZoneField::TuneCents, 250.0).tuneCents, 100.0, 1e-6);
    CHECK_NEAR(withZoneField(base, ZoneField::TuneCents, -250.0).tuneCents, -100.0, 1e-6);
    CHECK_NEAR(withZoneField(base, ZoneField::GainDb, -6.0).gainDb, -6.0, 1e-6);
    CHECK_NEAR(withZoneField(base, ZoneField::GainDb, -100.0).gainDb, zone_limits::kMinGainDb, 1e-6);
    CHECK_NEAR(withZoneField(base, ZoneField::GainDb, 40.0).gainDb, zone_limits::kMaxGainDb, 1e-6);

    CHECK_EQ(withZoneField(base, ZoneField::RrGroup, 3).rrGroup, 3);
    CHECK_EQ(withZoneField(base, ZoneField::RrGroup, -2).rrGroup, 0);
    CHECK_EQ(withZoneField(base, ZoneField::RrIndex, 500).rrIndex, zone_limits::kMaxRrIndex);

    // Other fields untouched by a single-field edit
    const Zone z = withZoneField(base, ZoneField::GainDb, 3.0);
    CHECK_EQ(z.rootKey, 60);
    CHECK_EQ(z.keyLow, 48);
    CHECK_EQ(z.keyHigh, 72);
    CHECK_EQ(z.sampleId, std::string("a"));

    // Field metadata
    double lo = 0, hi = 0;
    zoneFieldRange(ZoneField::VelLow, lo, hi);
    CHECK_EQ(lo, 1.0);
    CHECK_EQ(hi, 127.0);
    zoneFieldRange(ZoneField::KeyHigh, lo, hi);
    CHECK_EQ(lo, 0.0);
    CHECK_EQ(hi, 127.0);
    CHECK_EQ(zoneFieldValue(base, ZoneField::KeyHigh), 72.0);
    CHECK_EQ(std::string(zoneFieldName(ZoneField::TuneCents)), std::string("Fine tune"));
}

static void testNoInvertedRanges()
{
    std::cout << "testNoInvertedRanges\n";
    const Zone base = makeZone("a", 48, 60, 54, 40, 90);

    // Low above high drags high along (never inverted)
    Zone z = withZoneField(base, ZoneField::KeyLow, 70);
    CHECK_EQ(z.keyLow, 70);
    CHECK_EQ(z.keyHigh, 70);
    // High below low drags low along
    z = withZoneField(base, ZoneField::KeyHigh, 30);
    CHECK_EQ(z.keyLow, 30);
    CHECK_EQ(z.keyHigh, 30);
    z = withZoneField(base, ZoneField::VelLow, 100);
    CHECK_EQ(z.velLow, 100);
    CHECK_EQ(z.velHigh, 100);
    z = withZoneField(base, ZoneField::VelHigh, 10);
    CHECK_EQ(z.velLow, 10);
    CHECK_EQ(z.velHigh, 10);

    // sanitizeZone repairs a legacy/hand-edited inverted zone by swapping
    Zone bad = makeZone("b", 80, 40, 300, 120, 5);
    bad.tuneCents = 999.0f;
    bad.gainDb = std::numeric_limits<float>::quiet_NaN();
    CHECK(sanitizeZone(bad));
    CHECK_EQ(bad.keyLow, 40);
    CHECK_EQ(bad.keyHigh, 80);
    CHECK_EQ(bad.velLow, 5);
    CHECK_EQ(bad.velHigh, 120);
    CHECK_EQ(bad.rootKey, 127);
    CHECK_NEAR(bad.tuneCents, 100.0, 1e-6);
    CHECK_NEAR(bad.gainDb, 0.0, 1e-6);
    Zone ok = base;
    CHECK(!sanitizeZone(ok));

    // makeZoneEdit also sanitizes a proposed zone
    InstrumentMap map;
    map.zones.push_back(base);
    Zone proposed = base;
    proposed.keyLow = 90;
    proposed.keyHigh = 20;
    const auto e = makeZoneEdit(map, 0, proposed);
    CHECK(e.has_value());
    if (e)
    {
        CHECK_EQ(e->after.keyLow, 20);
        CHECK_EQ(e->after.keyHigh, 90);
    }
}

static void testOverlapWarnings()
{
    std::cout << "testOverlapWarnings\n";
    InstrumentMap map;
    map.zones.push_back(makeZone("a", 48, 60, 54));                 // 0
    map.zones.push_back(makeZone("b", 55, 70, 62));                 // 1 overlaps a (55-60)
    map.zones.push_back(makeZone("c", 80, 90, 85, 1, 64));          // 2 soft layer
    map.zones.push_back(makeZone("d", 80, 90, 85, 65, 127));        // 3 loud layer: no overlap with c
    map.zones.push_back(makeZone("e", 100, 100, 100, 1, 127, 7, 0)); // 4 RR alt
    map.zones.push_back(makeZone("f", 100, 100, 100, 1, 127, 7, 1)); // 5 RR alt (same group)
    map.zones.push_back(makeZone("g", 100, 100, 100, 1, 127, 8, 0)); // 6 other group -> overlaps

    auto o = findOverlaps(map, 0);
    CHECK_EQ(o.size(), size_t(1));
    if (!o.empty()) CHECK_EQ(o[0], size_t(1));
    CHECK(findOverlaps(map, 2).empty());
    CHECK(findOverlaps(map, 3).empty());
    o = findOverlaps(map, 4);
    CHECK_EQ(o.size(), size_t(1));          // g only; f is an intended alternate
    if (!o.empty()) CHECK_EQ(o[0], size_t(6));
    CHECK_EQ(findOverlaps(map, 6).size(), size_t(2));
    CHECK(findOverlaps(map, 99).empty());

    // Edits that create an overlap are allowed (warn, don't block)
    const auto e = makeZoneEdit(map, 2, withZoneField(map.zones[2], ZoneField::VelHigh, 100));
    CHECK(e.has_value());
    if (e) CHECK(applyZoneEdit(map, *e));
    CHECK_EQ(findOverlaps(map, 2).size(), size_t(1));
}

static void testResetToAuto()
{
    std::cout << "testResetToAuto\n";
    InstrumentMap map;
    map.zones.push_back(makeZone("a", 48, 60, 54));
    map.zones.push_back(makeZone("b", 61, 72, 66, 1, 127, 2, 1));
    map.zones[1].tuneCents = -15.0f;
    stampAutoValues(map);
    CHECK(map.zones[0].autoValues.has_value());
    CHECK(!isZoneEditedFromAuto(map.zones[0]));

    Zone z = map.zones[1];
    z = withZoneField(z, ZoneField::RootKey, 70);
    z = withZoneField(z, ZoneField::KeyLow, 58);
    z = withZoneField(z, ZoneField::TuneCents, 4.0);
    z = withZoneField(z, ZoneField::GainDb, -3.0);
    z = withZoneField(z, ZoneField::RrIndex, 4);
    CHECK(isZoneEditedFromAuto(z));
    const Zone r = resetZoneToAuto(z);
    CHECK(!isZoneEditedFromAuto(r));
    CHECK_EQ(r.rootKey, 66);
    CHECK_EQ(r.keyLow, 61);
    CHECK_EQ(r.keyHigh, 72);
    CHECK_NEAR(r.tuneCents, -15.0, 1e-6);
    CHECK_NEAR(r.gainDb, 0.0, 1e-6);
    CHECK_EQ(r.rrGroup, 2);
    CHECK_EQ(r.rrIndex, 1);
    CHECK(r.autoValues.has_value());   // snapshot survives the reset

    // Zones from older patches have no snapshot: reset is a no-op, never "edited"
    Zone legacy = makeZone("c", 0, 127, 60);
    legacy.rootKey = 62;
    CHECK(!isZoneEditedFromAuto(legacy));
    CHECK(sameZoneSettings(resetZoneToAuto(legacy), legacy));
    CHECK(!makeZoneEdit(InstrumentMap { { legacy } }, 0, resetZoneToAuto(legacy)).has_value());
}

static void testUseDetectedPitch()
{
    std::cout << "testUseDetectedPitch\n";
    SampleRef ref;
    ref.id = "a";
    ref.detectedRootKey = 52;    // E3
    ref.detectedCents = 15.0f;   // 15 ct sharp
    ref.pitchConfidence = 0.93f;
    const auto p = detectedPitchFor(ref);
    CHECK(p.has_value());
    if (!p) return;
    CHECK_EQ(p->rootKey, 52);
    CHECK_NEAR(p->cents, 15.0, 1e-6);
    CHECK_NEAR(p->confidence, 0.93, 1e-6);

    const Zone z = makeZone("a", 40, 64, 48);
    CHECK(!zoneUsesDetectedPitch(z, *p));
    const Zone d = withDetectedPitch(z, *p);
    CHECK_EQ(d.rootKey, 52);
    CHECK_NEAR(d.tuneCents, -15.0, 1e-6);     // play it 15 ct lower
    CHECK_EQ(d.keyLow, 40);                    // range untouched
    CHECK_EQ(d.keyHigh, 64);
    CHECK(zoneUsesDetectedPitch(d, *p));

    SampleRef none;
    none.id = "kick";
    none.pitchSource = PitchSource::Unpitched;
    CHECK(!detectedPitchFor(none).has_value());

    SampleRef noConf;
    noConf.detectedRootKey = 200;              // legacy garbage -> clamped
    const auto q = detectedPitchFor(noConf);
    CHECK(q.has_value());
    if (q)
    {
        CHECK_EQ(q->rootKey, 127);
        CHECK_NEAR(q->confidence, 1.0, 1e-6);
        CHECK_NEAR(q->cents, 0.0, 1e-6);
    }
}

static void testEditRecords()
{
    std::cout << "testEditRecords\n";
    InstrumentMap map;
    map.zones.push_back(makeZone("a", 48, 60, 54));
    map.zones.push_back(makeZone("b", 61, 72, 66));

    CHECK(!makeZoneEdit(map, 0, map.zones[0]).has_value());          // no change
    CHECK(!makeZoneEdit(map, 5, map.zones[0]).has_value());          // bad index
    Zone wrong = map.zones[1];
    CHECK(!makeZoneEdit(map, 0, wrong).has_value());                 // different sample id

    auto e = makeZoneEdit(map, 1, withZoneField(map.zones[1], ZoneField::RootKey, 67));
    CHECK(e.has_value());
    if (!e) return;
    CHECK_EQ(e->index, size_t(1));
    CHECK_EQ(e->sampleId, std::string("b"));
    CHECK_EQ(e->before.rootKey, 66);
    CHECK_EQ(e->after.rootKey, 67);
    CHECK(applyZoneEdit(map, *e, true));
    CHECK_EQ(map.zones[1].rootKey, 67);
    CHECK(applyZoneEdit(map, *e, false));
    CHECK_EQ(map.zones[1].rootKey, 66);

    // Stale record after the map was replaced (new import) is refused
    InstrumentMap other;
    other.zones.push_back(makeZone("x", 0, 127, 60));
    other.zones.push_back(makeZone("y", 0, 127, 60));
    CHECK(!applyZoneEdit(other, *e, true));
    CHECK_EQ(other.zones[1].rootKey, 60);
}

static void testPersistenceRoundTrip()
{
    std::cout << "testPersistenceRoundTrip\n";
    Patch patch;
    patch.name = "Edited";
    SampleRef s;
    s.id = "a";
    s.path = "/tmp/a.wav";
    s.displayName = "a.wav";
    s.detectedRootKey = 52;
    s.detectedCents = 15.0f;
    patch.samples.push_back(s);
    patch.map.zones.push_back(makeZone("a", 40, 64, 52, 1, 100, 3, 2));
    patch.map.zones[0].tuneCents = -15.0f;
    stampAutoValues(patch.map);
    // User edits after import
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::RootKey, 53);
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::KeyHigh, 70);
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::VelLow, 20);
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::TuneCents, 7.5);
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::GainDb, -4.25);
    patch.map.zones[0] = withZoneField(patch.map.zones[0], ZoneField::RrIndex, 5);
    patch.map.zones.push_back(makeZone("legacy", 0, 127, 60));      // no auto values

    const std::string json = PatchStore::toJson(patch);
    CHECK(json.find("\"auto\"") != std::string::npos);
    const auto back = PatchStore::fromJson(json);
    CHECK(back.has_value());
    if (!back) return;
    CHECK_EQ(back->map.zones.size(), size_t(2));
    const Zone& z = back->map.zones[0];
    CHECK_EQ(z.rootKey, 53);
    CHECK_EQ(z.keyLow, 40);
    CHECK_EQ(z.keyHigh, 70);
    CHECK_EQ(z.velLow, 20);
    CHECK_EQ(z.velHigh, 100);
    CHECK_EQ(z.rrGroup, 3);
    CHECK_EQ(z.rrIndex, 5);
    CHECK_NEAR(z.tuneCents, 7.5, 1e-4);
    CHECK_NEAR(z.gainDb, -4.25, 1e-4);
    CHECK(z.autoValues.has_value());
    if (z.autoValues)
    {
        CHECK_EQ(z.autoValues->rootKey, 52);
        CHECK_EQ(z.autoValues->keyHigh, 64);
        CHECK_EQ(z.autoValues->velLow, 1);
        CHECK_EQ(z.autoValues->rrIndex, 2);
        CHECK_NEAR(z.autoValues->tuneCents, -15.0, 1e-4);
    }
    CHECK(isZoneEditedFromAuto(z));
    CHECK(!back->map.zones[1].autoValues.has_value());
    // Deterministic: re-serialising gives identical JSON
    CHECK(PatchStore::toJson(*back) == json);

    // Reset to auto survives a save/load cycle through a real file
    const auto dir = std::filesystem::temp_directory_path() / "looper-zone-editor-test";
    std::filesystem::create_directories(dir);
    const auto file = (dir / "edited.looper.json").string();
    CHECK(PatchStore::save(file, *back));
    const auto loaded = PatchStore::load(file);
    CHECK(loaded.has_value());
    if (loaded)
    {
        const Zone& lz = loaded->patch.map.zones[0];
        CHECK_EQ(lz.rootKey, 53);
        const Zone reset = resetZoneToAuto(lz);
        CHECK_EQ(reset.rootKey, 52);
        CHECK_EQ(reset.keyHigh, 64);
        CHECK_NEAR(reset.tuneCents, -15.0, 1e-4);
    }
    std::filesystem::remove_all(dir);

    // Older patch JSON (no "auto" key) still loads; zones simply have no snapshot
    const std::string legacy = R"({"schemaVersion":1,"name":"Old","patchRoot":".","samples":[],
        "map":{"zones":[{"sampleId":"s","rootKey":61,"keyLow":0,"keyHigh":127,"velLow":1,"velHigh":127}]}})";
    const auto old = PatchStore::fromJson(legacy);
    CHECK(old.has_value());
    if (old)
    {
        CHECK_EQ(old->map.zones.size(), size_t(1));
        CHECK(!old->map.zones[0].autoValues.has_value());
        CHECK_EQ(old->map.zones[0].rootKey, 61);
    }
}

struct LiveRig
{
    SamplePool pool;
    VoiceEngine engine;
    std::shared_ptr<InstrumentMap> map;
    std::vector<float> l, r;

    LiveRig()
    {
        pool.setBuffer("a", sineBuffer(220.0));
        pool.setBuffer("b", sineBuffer(330.0));
        map = std::make_shared<InstrumentMap>();
        map->zones.push_back(makeZone("a", 48, 64, 60));
        map->zones.push_back(makeZone("b", 65, 84, 72));
        engine.setSampleRate(44100.0);
        engine.setSamplePool(&pool);
        engine.adoptMap(map);
        l.assign(256, 0.0f);
        r.assign(256, 0.0f);
    }

    void block() { engine.processBlock(l.data(), r.data(), static_cast<int>(l.size())); }

    /** Message-thread style: copy, edit zone i, queue live. */
    void edit(size_t i, const Zone& z)
    {
        auto next = std::make_shared<InstrumentMap>(*map);
        CHECK(replaceZoneInMap(*next, i, next->zones[i].sampleId, z));
        map = next;
        engine.updateMapLive(next);
    }
};

static void testLiveVoiceUpdates()
{
    std::cout << "testLiveVoiceUpdates\n";
    LiveRig rig;
    rig.engine.noteOn(60, 100, 1);   // zone a, root 60 -> ratio 1
    rig.engine.noteOn(72, 100, 1);   // zone b, root 72 -> ratio 1
    rig.block();
    const Voice* va = rig.engine.findActiveVoice(60, 1);
    const Voice* vb = rig.engine.findActiveVoice(72, 1);
    CHECK(va != nullptr && vb != nullptr);
    if (!va || !vb) return;
    CHECK_EQ(va->zoneIndex, 0);
    CHECK_EQ(vb->zoneIndex, 1);
    CHECK_NEAR(va->pitchRatio, 1.0, 1e-9);

    // Root key down an octave + 6 dB gain on zone a while it sounds
    Zone za = withZoneField(rig.map->zones[0], ZoneField::RootKey, 48);
    za = withZoneField(za, ZoneField::GainDb, 6.0);
    rig.edit(0, za);
    CHECK_NEAR(va->pitchRatio, 1.0, 1e-9);           // nothing changes until the audio thread adopts it

    const long allocs0 = g_allocs.load();
    const long frees0 = g_frees.load();
    rig.block();                                     // adopts the queued map
    CHECK_EQ(g_allocs.load() - allocs0, 0L);         // real-time safe hand-off
    CHECK_EQ(g_frees.load() - frees0, 0L);           // old map parked, freed off the audio thread
    CHECK_NEAR(va->pitchRatio, 2.0, 1e-9);
    CHECK_NEAR(va->zoneGainLin, std::pow(10.0, 6.0 / 20.0), 1e-4);
    CHECK_EQ(va->zone.rootKey, 48);
    CHECK_NEAR(vb->pitchRatio, 1.0, 1e-9);           // other zone's voice untouched
    CHECK_NEAR(vb->zoneGainLin, 1.0, 1e-6);

    // Fine tune +50 ct on zone b
    rig.edit(1, withZoneField(rig.map->zones[1], ZoneField::TuneCents, 50.0));
    rig.block();
    CHECK_NEAR(vb->pitchRatio, ratioForSemis(0.5), 1e-9);

    // Shrinking the key range away from a sounding note does not cut it off
    rig.edit(0, withZoneField(rig.map->zones[0], ZoneField::KeyHigh, 55));
    rig.block();
    CHECK(rig.engine.findActiveVoice(60, 1) != nullptr);
    CHECK_EQ(rig.engine.findActiveVoice(60, 1)->zone.keyHigh, 55);
    // ...but the next note-on uses the new range: 60 is now outside zone a and b -> silent
    rig.engine.noteOff(60, 1);
    rig.engine.noteOn(58, 100, 1);
    const Voice* v58 = rig.engine.findActiveVoice(58, 1);    // 58 > 55 and < 65: no zone
    CHECK(v58 == nullptr || v58->zoneIndex == -1);           // (engine's demo-tone fallback at most)
    rig.engine.noteOn(50, 100, 1);
    const Voice* v50 = rig.engine.findActiveVoice(50, 1);
    CHECK(v50 != nullptr);
    if (v50) CHECK_NEAR(v50->pitchRatio, ratioForSemis(50 - 48), 1e-9);

    // Several queued edits before one block: the newest wins
    rig.edit(1, withZoneField(rig.map->zones[1], ZoneField::GainDb, -12.0));
    rig.edit(1, withZoneField(rig.map->zones[1], ZoneField::GainDb, -6.0));
    CHECK(rig.engine.applyPendingMapUpdate());
    CHECK(!rig.engine.applyPendingMapUpdate());      // nothing left
    CHECK_NEAR(vb->zoneGainLin, std::pow(10.0, -6.0 / 20.0), 1e-4);

    // A wholesale swap (import / patch load) supersedes a queued live edit
    rig.edit(1, withZoneField(rig.map->zones[1], ZoneField::GainDb, -20.0));
    auto fresh = std::make_shared<InstrumentMap>(*rig.map);
    fresh->zones[1].gainDb = 0.0f;
    rig.engine.adoptMap(fresh);
    CHECK(!rig.engine.applyPendingMapUpdate());
}

static void testLiveEditKeepsRoundRobinPosition()
{
    std::cout << "testLiveEditKeepsRoundRobinPosition\n";
    SamplePool pool;
    pool.setBuffer("r0", sineBuffer());
    pool.setBuffer("r1", sineBuffer());
    pool.setBuffer("r2", sineBuffer());
    auto map = std::make_shared<InstrumentMap>();
    map->zones.push_back(makeZone("r0", 60, 60, 60, 1, 127, 1, 0));
    map->zones.push_back(makeZone("r1", 60, 60, 60, 1, 127, 1, 1));
    map->zones.push_back(makeZone("r2", 60, 60, 60, 1, 127, 1, 2));
    VoiceEngine e;
    e.setSamplePool(&pool);
    e.adoptMap(map);
    CHECK_EQ(e.selectZone(60, 100)->sampleId, std::string("r0"));
    CHECK_EQ(e.selectZone(60, 100)->sampleId, std::string("r1"));
    auto next = std::make_shared<InstrumentMap>(*map);
    next->zones[0].gainDb = -3.0f;
    e.updateMapLive(next);
    CHECK(e.applyPendingMapUpdate());
    CHECK_EQ(e.selectZone(60, 100)->sampleId, std::string("r2"));   // cycle continues
    CHECK_NEAR(e.selectZone(60, 100)->gainDb, -3.0, 1e-6);           // r0 again, edited
}

static void testAuditionNote()
{
    std::cout << "testAuditionNote\n";
    int note = 0, vel = 0;
    auditionNoteFor(makeZone("a", 48, 60, 54), note, vel);
    CHECK_EQ(note, 54);
    CHECK_EQ(vel, 100);
    auditionNoteFor(makeZone("a", 48, 60, 72), note, vel);   // root above range -> top key
    CHECK_EQ(note, 60);
    auditionNoteFor(makeZone("a", 48, 60, 30, 1, 40), note, vel); // soft layer
    CHECK_EQ(note, 48);
    CHECK_EQ(vel, 40);
    auditionNoteFor(makeZone("a", 48, 60, 50, 110, 127), note, vel); // loud layer
    CHECK_EQ(vel, 110);
}

#if LOOPER_ZONE_EDIT_WITH_JUCE
/** Stands in for LooperAudioProcessor: owns the map, pushes edits live to a VoiceEngine. */
struct FakeTarget : ZoneEditTarget
{
    LiveRig rig;
    int replaced = 0;
    bool replaceZone(size_t index, const std::string& sampleId, const Zone& zone) override
    {
        auto next = std::make_shared<InstrumentMap>(*rig.map);
        if (!replaceZoneInMap(*next, index, sampleId, zone))
            return false;
        rig.map = next;
        rig.engine.updateMapLive(next);
        ++replaced;
        return true;
    }
    const Zone& zone(size_t i) const { return rig.map->zones[i]; }
};

static bool performEdit(juce::UndoManager& um, FakeTarget& t, size_t index, const Zone& proposed,
                        bool newTransaction = true)
{
    auto e = makeZoneEdit(*t.rig.map, index, proposed);
    if (!e)
        return false;
    if (newTransaction)
        um.beginNewTransaction();
    return um.perform(new ZoneEditAction(t, std::move(*e)));
}

static void testJuceUndoManager()
{
    std::cout << "testJuceUndoManager\n";
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeTarget t;
    juce::UndoManager um;
    t.rig.engine.noteOn(60, 100, 1);
    t.rig.block();

    CHECK(performEdit(um, t, 0, withZoneField(t.zone(0), ZoneField::RootKey, 48)));
    CHECK_EQ(t.zone(0).rootKey, 48);
    t.rig.block();
    CHECK_NEAR(t.rig.engine.findActiveVoice(60, 1)->pitchRatio, 2.0, 1e-9);
    CHECK(performEdit(um, t, 1, withZoneField(t.zone(1), ZoneField::VelLow, 64)));
    CHECK_EQ(t.zone(1).velLow, 64);
    CHECK(um.canUndo());

    CHECK(um.undo());
    CHECK_EQ(t.zone(1).velLow, 1);
    CHECK_EQ(t.zone(0).rootKey, 48);
    CHECK(um.undo());
    CHECK_EQ(t.zone(0).rootKey, 60);
    t.rig.block();
    CHECK_NEAR(t.rig.engine.findActiveVoice(60, 1)->pitchRatio, 1.0, 1e-9);   // undo is live too
    CHECK(!um.canUndo());
    CHECK(um.canRedo());
    CHECK(um.redo());
    CHECK_EQ(t.zone(0).rootKey, 48);
    CHECK(um.redo());
    CHECK_EQ(t.zone(1).velLow, 64);
    CHECK(!um.canRedo());

    // A slider drag = many edits in ONE transaction -> coalesced -> one undo step
    um.beginNewTransaction("Gain");
    for (int i = 1; i <= 8; ++i)
        CHECK(performEdit(um, t, 0, withZoneField(t.zone(0), ZoneField::GainDb, -1.0 * i), false));
    CHECK_EQ(um.getNumActionsInCurrentTransaction(), 1);
    CHECK_NEAR(t.zone(0).gainDb, -8.0, 1e-6);
    CHECK(um.undo());
    CHECK_NEAR(t.zone(0).gainDb, 0.0, 1e-6);
    CHECK_EQ(t.zone(0).rootKey, 48);          // previous transaction untouched
    CHECK(um.redo());
    CHECK_NEAR(t.zone(0).gainDb, -8.0, 1e-6);

    // New edit after an undo clears the redo stack
    CHECK(um.undo());
    CHECK(performEdit(um, t, 0, withZoneField(t.zone(0), ZoneField::TuneCents, 12.0)));
    CHECK(!um.canRedo());
    CHECK_NEAR(t.zone(0).gainDb, 0.0, 1e-6);
    CHECK_NEAR(t.zone(0).tuneCents, 12.0, 1e-6);

    // Reset to auto / Use detected are ordinary undoable edits
    stampAutoValues(*t.rig.map);           // pretend this map came from an import
    t.rig.map->zones[0] = withZoneField(t.zone(0), ZoneField::KeyLow, 52);
    CHECK(performEdit(um, t, 0, resetZoneToAuto(t.zone(0))));
    CHECK_EQ(t.zone(0).keyLow, 48);
    CHECK(um.undo());
    CHECK_EQ(t.zone(0).keyLow, 52);
    DetectedPitchInfo p;
    p.rootKey = 57;
    p.cents = -20.0f;
    CHECK(performEdit(um, t, 0, withDetectedPitch(t.zone(0), p)));
    CHECK_EQ(t.zone(0).rootKey, 57);
    CHECK_NEAR(t.zone(0).tuneCents, 20.0, 1e-6);
    CHECK(um.undo());
    CHECK_NEAR(t.zone(0).tuneCents, 12.0, 1e-6);

    // History is cleared when the map is replaced; stale actions cannot corrupt a new map
    CHECK(performEdit(um, t, 1, withZoneField(t.zone(1), ZoneField::RootKey, 70)));
    auto other = std::make_shared<InstrumentMap>();
    other->zones.push_back(makeZone("x", 0, 127, 60));
    other->zones.push_back(makeZone("y", 0, 127, 60));
    t.rig.map = other;
    um.undo();                              // action refuses (sample id mismatch)...
    CHECK_EQ(t.zone(1).rootKey, 60);        // ...so the new map is untouched
    CHECK(!um.canUndo());                   // and JUCE drops the now-invalid history
    CHECK(!um.canRedo());
}
#endif

int main()
{
    testFieldEditsAndClamping();
    testNoInvertedRanges();
    testOverlapWarnings();
    testResetToAuto();
    testUseDetectedPitch();
    testEditRecords();
    testPersistenceRoundTrip();
    testLiveVoiceUpdates();
    testLiveEditKeepsRoundRobinPosition();
    testAuditionNote();
#if LOOPER_ZONE_EDIT_WITH_JUCE
    testJuceUndoManager();
#else
    std::cout << "(juce::UndoManager tests skipped: built without JUCE)\n";
#endif
    std::cout << "\nZoneEditorTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
