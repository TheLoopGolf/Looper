#include "../Source/AutoMapper/AutoMapper.h"
#include "../Source/Import/MapCommit.h"
#include <algorithm>
#include "../Source/AutoMapper/FilenameTokens.h"

#include <cassert>
#include <cmath>
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

static SampleRef makeSample(const std::string& id, const std::string& path)
{
    SampleRef s;
    s.id = id;
    s.path = path;
    s.displayName = path;
    return s;
}

static const Zone* findZone(const InstrumentMap& map, const std::string& sampleId)
{
    for (const auto& z : map.zones)
        if (z.sampleId == sampleId)
            return &z;
    return nullptr;
}

static void testNoteNameToMidi()
{
    std::cout << "testNoteNameToMidi\n";
    CHECK(noteNameToMidi("C4").value_or(-1) == 60);
    CHECK(noteNameToMidi("c4").value_or(-1) == 60);
    CHECK(noteNameToMidi("A3").value_or(-1) == 57);
    CHECK(noteNameToMidi("c1").value_or(-1) == 24);
    CHECK(noteNameToMidi("Bb2").value_or(-1) == 46);
    CHECK(noteNameToMidi("c#3").value_or(-1) == 49);
    CHECK(noteNameToMidi("D4").value_or(-1) == 62);
    CHECK(midiToNoteName(60) == "C4");
    CHECK(midiToNoteName(57) == "A3");
}

static void testFilenameVelocityAndRr()
{
    std::cout << "testFilenameVelocityAndRr\n";
    {
        auto t = parseFilenameTokens("Piano_C4_v32.wav");
        CHECK(t.midiNote.value_or(-1) == 60);
        CHECK(t.velocityHint.value_or(-1) == 32);
    }
    {
        auto t = parseFilenameTokens("Piano_C4_v96.wav");
        CHECK(t.midiNote.value_or(-1) == 60);
        CHECK(t.velocityHint.value_or(-1) == 96);
    }
    {
        auto t = parseFilenameTokens("Pad_soft_A3.wav");
        CHECK(t.midiNote.value_or(-1) == 57);
        CHECK(t.velocityHint.value_or(-1) == 32);
    }
    {
        auto t = parseFilenameTokens("Pad_hard_A3.wav");
        CHECK(t.midiNote.value_or(-1) == 57);
        CHECK(t.velocityHint.value_or(-1) == 96);
    }
    {
        auto t = parseFilenameTokens("snare_rr1.wav");
        CHECK(t.rrIndex.value_or(-1) == 1);
        CHECK(!t.midiNote.has_value());
    }
    {
        auto t = parseFilenameTokens("kick_c1.wav");
        CHECK(t.midiNote.value_or(-1) == 24);
    }
    {
        auto t = parseFilenameTokens("tom_round_2.wav");
        CHECK(t.rrIndex.value_or(-1) == 2);
    }
    {
        auto t = parseFilenameTokens("hat_alt_3.wav");
        CHECK(t.rrIndex.value_or(-1) == 3);
    }
    {
        auto t = parseFilenameTokens("keys_velocity_64.wav");
        CHECK(t.velocityHint.value_or(-1) == 64);
    }
}

static void testPianoVelocityAndKeyRanges()
{
    std::cout << "testPianoVelocityAndKeyRanges\n";
    std::vector<SampleRef> samples = {
        makeSample("p1", "Piano_C4_v32.wav"),
        makeSample("p2", "Piano_C4_v96.wav"),
        makeSample("p3", "Piano_D4_v64.wav"),
    };

    auto result = AutoMapper::map(samples);
    CHECK_EQ(int(result.map.zones.size()), 3);

    const Zone* c4soft = findZone(result.map, "p1");
    const Zone* c4loud = findZone(result.map, "p2");
    const Zone* d4 = findZone(result.map, "p3");
    CHECK(c4soft != nullptr);
    CHECK(c4loud != nullptr);
    CHECK(d4 != nullptr);

    CHECK_EQ(c4soft->rootKey, 60);
    CHECK_EQ(c4loud->rootKey, 60);
    CHECK_EQ(d4->rootKey, 62);

    // Velocity midpoint between 32 and 96 = 64 → soft 1–64, loud 65–127
    CHECK_EQ(c4soft->velLow, 1);
    CHECK_EQ(c4soft->velHigh, 64);
    CHECK_EQ(c4loud->velLow, 65);
    CHECK_EQ(c4loud->velHigh, 127);

    // D4 single vel → full range
    CHECK_EQ(d4->velLow, 1);
    CHECK_EQ(d4->velHigh, 127);

    // Key midpoint between 60 and 62 = 61 → C4: 0–61, D4: 62–127
    CHECK_EQ(c4soft->keyLow, 0);
    CHECK_EQ(c4soft->keyHigh, 61);
    CHECK_EQ(c4loud->keyLow, 0);
    CHECK_EQ(c4loud->keyHigh, 61);
    CHECK_EQ(d4->keyLow, 62);
    CHECK_EQ(d4->keyHigh, 127);
}

static void testKickSingle()
{
    std::cout << "testKickSingle\n";
    auto result = AutoMapper::map({makeSample("k", "kick_c1.wav")});
    CHECK_EQ(int(result.map.zones.size()), 1);
    const auto& z = result.map.zones[0];
    CHECK_EQ(z.rootKey, 24);
    CHECK_EQ(z.keyLow, 0);
    CHECK_EQ(z.keyHigh, 127);
    CHECK_EQ(z.velLow, 1);
    CHECK_EQ(z.velHigh, 127);
}

static void testSnareRoundRobin()
{
    std::cout << "testSnareRoundRobin\n";
    std::vector<SampleRef> samples = {
        makeSample("s1", "snare_rr1.wav"),
        makeSample("s2", "snare_rr2.wav"),
        makeSample("s3", "snare_rr3.wav"),
        makeSample("s4", "snare_rr4.wav"),
    };
    auto result = AutoMapper::map(samples);
    CHECK_EQ(int(result.map.zones.size()), 4);

    // Same implied root (spread default 60), full key/vel, shared RR group
    int rrGroup = result.map.zones[0].rrGroup;
    CHECK(rrGroup != 0);
    std::vector<int> indices;
    for (const auto& z : result.map.zones)
    {
        CHECK_EQ(z.rootKey, 60);
        CHECK_EQ(z.keyLow, 0);
        CHECK_EQ(z.keyHigh, 127);
        CHECK_EQ(z.velLow, 1);
        CHECK_EQ(z.velHigh, 127);
        CHECK_EQ(z.rrGroup, rrGroup);
        indices.push_back(z.rrIndex);
    }
    std::sort(indices.begin(), indices.end());
    CHECK_EQ(indices[0], 1);
    CHECK_EQ(indices[1], 2);
    CHECK_EQ(indices[2], 3);
    CHECK_EQ(indices[3], 4);
}

static void testPadSoftHard()
{
    std::cout << "testPadSoftHard\n";
    auto result = AutoMapper::map({
        makeSample("a", "Pad_soft_A3.wav"),
        makeSample("b", "Pad_hard_A3.wav"),
    });
    CHECK_EQ(int(result.map.zones.size()), 2);
    const Zone* soft = findZone(result.map, "a");
    const Zone* hard = findZone(result.map, "b");
    CHECK(soft && hard);
    CHECK_EQ(soft->rootKey, 57);
    CHECK_EQ(hard->rootKey, 57);
    CHECK_EQ(soft->velLow, 1);
    CHECK_EQ(soft->velHigh, 64);
    CHECK_EQ(hard->velLow, 65);
    CHECK_EQ(hard->velHigh, 127);
    CHECK_EQ(soft->keyLow, 0);
    CHECK_EQ(soft->keyHigh, 127);
}

static void testEmptyAndDeterministic()
{
    std::cout << "testEmptyAndDeterministic\n";
    auto empty = AutoMapper::map({});
    CHECK_EQ(int(empty.map.zones.size()), 0);

    std::vector<SampleRef> samples = {
        makeSample("p3", "Piano_D4_v64.wav"),
        makeSample("p1", "Piano_C4_v32.wav"),
        makeSample("p2", "Piano_C4_v96.wav"),
    };
    auto a = AutoMapper::map(samples);
    auto b = AutoMapper::map(samples);
    CHECK_EQ(int(a.map.zones.size()), int(b.map.zones.size()));
    for (size_t i = 0; i < a.map.zones.size(); ++i)
    {
        CHECK_EQ(a.map.zones[i].sampleId, b.map.zones[i].sampleId);
        CHECK_EQ(a.map.zones[i].rootKey, b.map.zones[i].rootKey);
        CHECK_EQ(a.map.zones[i].keyLow, b.map.zones[i].keyLow);
        CHECK_EQ(a.map.zones[i].keyHigh, b.map.zones[i].keyHigh);
        CHECK_EQ(a.map.zones[i].velLow, b.map.zones[i].velLow);
        CHECK_EQ(a.map.zones[i].velHigh, b.map.zones[i].velHigh);
    }
}

static void testPitchDetectStub()
{
    std::cout << "testPitchDetectStub\n";
    auto r = AutoMapper::detectPitchStub(makeSample("g", "garbage_xyz.wav"));
    CHECK(!r.pitchHz.has_value());
    CHECK(!r.rootKey.has_value());
    CHECK(r.confidence < 0.5f);
}

static void testDuplicateWarn()
{
    std::cout << "testDuplicateWarn\n";
    auto result = AutoMapper::map({
        makeSample("d1", "Piano_C4_v32.wav"),
        makeSample("d2", "Piano_C4_v32_copy.wav"),
    });
    // Both parse to same root+vel+rr → warning expected
    bool warned = !result.globalWarnings.empty();
    for (const auto& rev : result.reviews)
        for (const auto& w : rev.warnings)
            if (w.find("Duplicate") != std::string::npos)
                warned = true;
    CHECK(warned);
}


static void testCommitAutoMapToInstrument()
{
    std::cout << "testCommitAutoMapToInstrument\n";
    auto result = AutoMapper::map({
        makeSample("p1", "Piano_C4_v32.wav"),
        makeSample("p2", "Piano_C4_v96.wav"),
        makeSample("p3", "Piano_D4_v64.wav"),
    });
    CHECK(result.map.zones.size() >= 3);

    InstrumentMap dest;
    dest.polyphonyLimit = 32;
    dest.zones.push_back(Zone{}); // will be replaced
    commitAutoMapToInstrument(dest, result);
    CHECK_EQ(int(dest.zones.size()), int(result.map.zones.size()));
    CHECK_EQ(dest.polyphonyLimit, 32); // preserved when result has default 0/64 handling
    // result.map.polyphonyLimit is typically 0 (unset) — commit keeps dest when result <= 0
    // Our commit: if result.map.polyphonyLimit > 0 use it, else keep dest if > 0
    CHECK(countUniqueRoots(dest) >= 2);
    CHECK(findZone(dest, "p1") != nullptr);
}

int main()
{
    testNoteNameToMidi();
    testFilenameVelocityAndRr();
    testPianoVelocityAndKeyRanges();
    testKickSingle();
    testSnareRoundRobin();
    testPadSoftHard();
    testEmptyAndDeterministic();
    testPitchDetectStub();
    testDuplicateWarn();
    testCommitAutoMapToInstrument();

    std::cout << "\nPassed: " << g_passed << "  Failed: " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
