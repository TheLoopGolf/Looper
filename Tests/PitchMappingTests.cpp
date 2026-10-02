// Unpitched chromatic (drum-kit) fallback, filename/audio mismatch flag, C3=60 naming,
// per-sample pitch metadata in .looper.json, and the matching SessionPrefs fields.
#include "../Source/AutoMapper/AutoMapper.h"
#include "../Source/AutoMapper/FilenameTokens.h"
#include "../Source/AutoMapper/PitchDetector.h"
#include "../Source/PatchStore/PatchStore.h"
#include "../Source/Prefs/SessionPrefs.h"
#include "SynthSignals.h"

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

static const Zone* findZone(const InstrumentMap& map, const std::string& id)
{
    for (const auto& z : map.zones)
        if (z.sampleId == id) return &z;
    return nullptr;
}

static const SampleReview* findReview(const AutoMapResult& r, const std::string& id)
{
    for (const auto& rev : r.reviews)
        if (rev.sampleId == id) return &rev;
    return nullptr;
}

static int rootOf(const AutoMapResult& r, const std::string& id)
{
    const Zone* z = findZone(r.map, id);
    return z ? z->rootKey : -1;
}

static PitchAnalysis analyse(const std::vector<float>& mono, double sr = 44100.0)
{
    return PitchDetector::analyzeMono(mono.data(), mono.size(), sr);
}

static PitchAnalysis fakePitch(int midi, float cents, float confidence)
{
    PitchAnalysis a;
    a.analysed = true;
    a.unpitched = false;
    a.midiNote = midi;
    a.cents = cents;
    a.confidence = confidence;
    a.f0Hz = 440.0 * std::pow(2.0, (midi + cents / 100.0 - 69.0) / 12.0);
    a.reason = "ok";
    return a;
}

// ---------------------------------------------------------------------------

static void testNaturalSortAndGroupNames()
{
    std::cout << "testNaturalSortAndGroupNames\n";
    CHECK(naturalLess("tom 2", "tom 10"));
    CHECK(!naturalLess("tom 10", "tom 2"));
    CHECK(naturalLess("Hat", "kick"));          // case-insensitive
    CHECK(naturalLess("perc9", "perc10"));
    CHECK(!naturalLess("a", "a"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Snare_rr2_v96.wav")), std::string("snare"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Snare rr1.wav")), std::string("snare"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Kick_soft.wav")), std::string("kick"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Kick_round_2.wav")), std::string("kick"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Tom 2.wav")), std::string("tom 2"));
    CHECK_EQ(layerGroupName(parseFilenameTokens("Hat closed vel_64.wav")), std::string("hat closed"));
}

static void testDrumKitSpreadOrdering()
{
    std::cout << "testDrumKitSpreadOrdering\n";
    // Deliberately unsorted input; no analyses at all (not even detection).
    std::vector<SampleRef> samples = {
        makeSample("t10", "Tom 10.wav"),
        makeSample("t2", "Tom 2.wav"),
        makeSample("k", "Kick.wav"),
        makeSample("s", "Snare.wav"),
        makeSample("h", "Hat closed.wav"),
    };
    PitchAnalysisMap analyses;
    for (const auto& s : samples)
        analyses[s.id] = analyse(synth::noiseHit(44100.0, 0.4));
    for (const auto& kv : analyses)
        CHECK(kv.second.unpitched);

    const auto r = AutoMapper::map(samples, {}, &analyses); // default = chromatic from 36
    CHECK_EQ(int(r.map.zones.size()), 5);
    CHECK_EQ(rootOf(r, "h"), 36);
    CHECK_EQ(rootOf(r, "k"), 37);
    CHECK_EQ(rootOf(r, "s"), 38);
    CHECK_EQ(rootOf(r, "t2"), 39);
    CHECK_EQ(rootOf(r, "t10"), 40);
    for (const auto& z : r.map.zones)
    {
        CHECK_EQ(z.keyLow, z.rootKey);   // single-key zones
        CHECK_EQ(z.keyHigh, z.rootKey);
        CHECK_EQ(z.velLow, 1);
        CHECK_EQ(z.velHigh, 127);
        CHECK(z.tuneCents == 0.0f);
    }
    const auto* rev = findReview(r, "k");
    CHECK(rev != nullptr);
    if (rev)
    {
        CHECK(rev->source == PitchSource::Unpitched);
        CHECK(rev->chromaticKey);
        CHECK(rev->warnings.empty()); // drum keys are intentional: no warning noise
        const auto label = describePitch(*rev);
        std::cout << "  label: " << label << "\n";
        CHECK(label.rfind("Unpitched (", 0) == 0);
        CHECK(label.size() >= 3 && label.compare(label.size() - 3, 3, "C#2") == 0); // assigned key 37
    }
    CHECK(r.globalWarnings.empty());

    // Same input, different order → identical map (deterministic natural order)
    std::vector<SampleRef> reversed(samples.rbegin(), samples.rend());
    const auto r2 = AutoMapper::map(reversed, {}, &analyses);
    for (const auto& s : samples)
        CHECK_EQ(rootOf(r2, s.id), rootOf(r, s.id));

    // Configurable start note
    AutoMapOptions opt;
    opt.unpitchedStartNote = 48;
    const auto r3 = AutoMapper::map(samples, opt, &analyses);
    CHECK_EQ(rootOf(r3, "h"), 48);
    CHECK_EQ(rootOf(r3, "t10"), 52);

    // Near the top: continue downward below the start key when keys run out
    opt.unpitchedStartNote = 126;
    const auto r4 = AutoMapper::map(samples, opt, &analyses);
    CHECK_EQ(rootOf(r4, "h"), 126);
    CHECK_EQ(rootOf(r4, "k"), 127);
    CHECK_EQ(rootOf(r4, "s"), 125);
    CHECK_EQ(rootOf(r4, "t2"), 124);
    CHECK_EQ(rootOf(r4, "t10"), 123);

    // Without any analyses (filename-only map) the chromatic fallback still applies
    const auto r5 = AutoMapper::map(samples);
    CHECK_EQ(rootOf(r5, "h"), 36);
    CHECK_EQ(rootOf(r5, "t10"), 40);
}

static void testSpreadSkipsPitchedKeys()
{
    std::cout << "testSpreadSkipsPitchedKeys\n";
    std::vector<SampleRef> samples = {
        makeSample("b36", "Bass_C2.wav"),   // filename note → MIDI 36
        makeSample("b38", "Bass_D2.wav"),   // MIDI 38
        makeSample("kick", "Kick.wav"),
        makeSample("snare", "Snare.wav"),
        makeSample("clap", "Clap.wav"),
    };
    const auto r = AutoMapper::map(samples);
    CHECK_EQ(rootOf(r, "b36"), 36);
    CHECK_EQ(rootOf(r, "b38"), 38);
    CHECK_EQ(rootOf(r, "clap"), 37);   // 36 taken → 37
    CHECK_EQ(rootOf(r, "kick"), 39);   // 38 taken → 39
    CHECK_EQ(rootOf(r, "snare"), 40);
    // Pitched spans are clipped around the drum block [37..40]
    const Zone* b36 = findZone(r.map, "b36");
    const Zone* b38 = findZone(r.map, "b38");
    CHECK(b36 && b38);
    if (b36 && b38)
    {
        CHECK_EQ(b36->keyLow, 0);
        CHECK_EQ(b36->keyHigh, 36);
        CHECK_EQ(b38->keyLow, 38);     // root inside the block → its own key only
        CHECK_EQ(b38->keyHigh, 38);
    }

    // Pitched instrument above the block keeps everything above it
    const auto r2 = AutoMapper::map({ makeSample("p", "Piano_C4.wav"),
                                      makeSample("k", "Kick.wav"),
                                      makeSample("s", "Snare.wav") });
    CHECK_EQ(rootOf(r2, "k"), 36);
    CHECK_EQ(rootOf(r2, "s"), 37);
    const Zone* p = findZone(r2.map, "p");
    CHECK(p != nullptr);
    if (p)
    {
        CHECK_EQ(p->keyLow, 38);
        CHECK_EQ(p->keyHigh, 127);
    }
    // Drum keys don't count toward the "multi-instrument" span heuristic
    const auto r3 = AutoMapper::map({ makeSample("p", "Lead_C6.wav"), makeSample("k", "Kick.wav") });
    CHECK_EQ(rootOf(r3, "k"), 36);
    CHECK(r3.globalWarnings.empty());
}

static void testGroupedLayersShareKey()
{
    std::cout << "testGroupedLayersShareKey\n";
    std::vector<SampleRef> samples = {
        makeSample("s1", "Snare_rr1.wav"),
        makeSample("s2", "Snare_rr2.wav"),
        makeSample("s3", "Snare_rr3.wav"),
        makeSample("ks", "Kick_soft.wav"),
        makeSample("kh", "Kick_hard.wav"),
        makeSample("h", "Hat.wav"),
    };
    const auto r = AutoMapper::map(samples);
    CHECK_EQ(int(r.map.zones.size()), 6);
    CHECK_EQ(rootOf(r, "h"), 36);
    CHECK_EQ(rootOf(r, "ks"), 37);
    CHECK_EQ(rootOf(r, "kh"), 37);
    CHECK_EQ(rootOf(r, "s1"), 38);
    CHECK_EQ(rootOf(r, "s2"), 38);
    CHECK_EQ(rootOf(r, "s3"), 38);

    // Velocity layers on the shared kick key (soft 32 / hard 96 → split at 64)
    const Zone* ks = findZone(r.map, "ks");
    const Zone* kh = findZone(r.map, "kh");
    CHECK(ks && kh);
    if (ks && kh)
    {
        CHECK_EQ(ks->velLow, 1);
        CHECK_EQ(ks->velHigh, 64);
        CHECK_EQ(kh->velLow, 65);
        CHECK_EQ(kh->velHigh, 127);
        CHECK_EQ(ks->keyLow, 37);
        CHECK_EQ(ks->keyHigh, 37);
    }
    // Round-robin alternates on the shared snare key
    const Zone* s1 = findZone(r.map, "s1");
    const Zone* s3 = findZone(r.map, "s3");
    CHECK(s1 && s3);
    if (s1 && s3)
    {
        CHECK(s1->rrGroup != 0);
        CHECK_EQ(s1->rrGroup, s3->rrGroup);
        CHECK_EQ(s1->rrIndex, 1);
        CHECK_EQ(s3->rrIndex, 3);
    }
    for (const auto& rev : r.reviews)
        for (const auto& w : rev.warnings)
            CHECK(w.find("Duplicate") == std::string::npos);
}

static void testLegacyFixedRootOption()
{
    std::cout << "testLegacyFixedRootOption\n";
    AutoMapOptions opt;
    opt.unpitchedFallback = AutoMapOptions::UnpitchedFallback::FixedRoot;
    const auto r = AutoMapper::map({ makeSample("k", "Kick.wav"), makeSample("s", "Snare.wav") }, opt);
    CHECK_EQ(rootOf(r, "k"), 60);
    CHECK_EQ(rootOf(r, "s"), 60);
    const Zone* k = findZone(r.map, "k");
    CHECK(k && k->keyLow == 0 && k->keyHigh == 127);
    const auto* rev = findReview(r, "k");
    CHECK(rev && !rev->warnings.empty() && !rev->chromaticKey);
    CHECK(AutoMapOptions::UnpitchedFallback::SpreadWarn == AutoMapOptions::UnpitchedFallback::FixedRoot);
}

static void testMismatchFlag()
{
    std::cout << "testMismatchFlag\n";
    const double sr = 44100.0;
    std::vector<SampleRef> samples = {
        makeSample("third", "Lead_C4.wav"),    // audio E4 → "audio sounds E4"
        makeSample("octDn", "Bass_C3.wav"),    // audio C2 → an octave lower
        makeSample("octUp", "Bell_C4.wav"),    // audio C6 → 2 octaves higher
        makeSample("weak", "Pad_C4.wav"),      // E4 but conf 0.7 → no flag
        makeSample("inTune", "Keys_C4.wav"),   // C4 +30 ct → same note, no flag
        makeSample("noise", "Hit_C4.wav"),     // unpitched audio → no flag
    };
    PitchAnalysisMap a;
    a["third"] = analyse(synth::sine(synth::midiToHz(64), sr, 1.0));
    a["octDn"] = analyse(synth::saw(synth::midiToHz(36), sr, 1.0));
    a["octUp"] = fakePitch(84, 2.0f, 0.95f);
    a["weak"] = fakePitch(64, 0.0f, 0.70f);
    a["inTune"] = fakePitch(60, 30.0f, 0.97f);
    a["noise"] = analyse(synth::noiseHit(sr, 0.4));
    CHECK_EQ(a["third"].midiNote, 64);
    CHECK(a["third"].confidence >= 0.8f);
    CHECK_EQ(a["octDn"].midiNote, 36);
    CHECK(a["noise"].unpitched);

    const auto r = AutoMapper::map(samples, {}, &a);
    // Filename always wins; no automatic change
    CHECK_EQ(rootOf(r, "third"), 60);
    CHECK_EQ(rootOf(r, "octDn"), 48);
    CHECK_EQ(rootOf(r, "octUp"), 60);
    for (const auto& z : r.map.zones)
        CHECK(z.tuneCents == 0.0f);

    const auto* third = findReview(r, "third");
    CHECK(third && third->source == PitchSource::Filename && third->mismatch.has_value());
    if (third && third->mismatch)
    {
        CHECK_EQ(third->mismatch->text, std::string("Filename C4, audio sounds E4"));
        CHECK_EQ(third->mismatch->semitones, 4);
        CHECK(!third->mismatch->octave);
        CHECK(!third->warnings.empty() && third->warnings.front() == third->mismatch->text);
        CHECK(AutoMapper::canUseDetected(*third));
        CHECK_EQ(describePitch(*third), std::string("Filename C4"));
    }
    const auto* octDn = findReview(r, "octDn");
    CHECK(octDn && octDn->mismatch.has_value());
    if (octDn && octDn->mismatch)
    {
        CHECK(octDn->mismatch->octave);
        CHECK_EQ(octDn->mismatch->semitones, -12);
        CHECK_EQ(octDn->mismatch->text, std::string("Filename C3, audio sounds an octave lower (C2)"));
    }
    const auto* octUp = findReview(r, "octUp");
    CHECK(octUp && octUp->mismatch && octUp->mismatch->octave);
    if (octUp && octUp->mismatch)
        CHECK_EQ(octUp->mismatch->text, std::string("Filename C4, audio sounds 2 octaves higher (C6)"));

    for (const char* id : { "weak", "inTune", "noise" })
    {
        const auto* rev = findReview(r, id);
        CHECK(rev && !rev->mismatch.has_value());
        if (rev)
            for (const auto& w : rev->warnings)
                CHECK(w.find("audio sounds") == std::string::npos);
        CHECK(rev && !AutoMapper::canUseDetected(*rev));
    }

    CHECK_EQ(describeInterval(-1), std::string("1 semitone lower"));
    CHECK_EQ(describeInterval(7), std::string("7 semitones higher"));
    CHECK_EQ(describeInterval(12), std::string("an octave higher"));
    CHECK_EQ(describeInterval(-24), std::string("2 octaves lower"));

    // Threshold is an option (exactly at 0.8 flags)
    PitchAnalysisMap edge;
    edge["e"] = fakePitch(62, 0.0f, 0.8f);
    const auto re = AutoMapper::map({ makeSample("e", "Organ_C4.wav") }, {}, &edge);
    CHECK(re.reviews.size() == 1 && re.reviews[0].mismatch.has_value());

    // "Use detected" (Review row action): detection replaces the filename note
    AutoMapOptions opt;
    opt.useDetectedFor.insert("third");
    opt.useDetectedFor.insert("weak");   // not a mismatch row, but explicitly asked → still allowed
    opt.useDetectedFor.insert("noise");  // unpitched → cannot apply, stays filename
    const auto ru = AutoMapper::map(samples, opt, &a);
    CHECK_EQ(rootOf(ru, "third"), 64);
    CHECK_EQ(rootOf(ru, "noise"), 60);
    const auto* u = findReview(ru, "third");
    CHECK(u && u->source == PitchSource::Detected && u->filenameOverridden);
    CHECK(u && u->mismatch.has_value() && u->warnings.empty());
    CHECK(u && !AutoMapper::canUseDetected(*u));
    if (u)
    {
        const auto label = describePitch(*u);
        std::cout << "  override label: " << label << "\n";
        CHECK(label.rfind("Detected E4 (", 0) == 0);
        CHECK(label.find("(filename C4 overridden)") != std::string::npos);
    }
}

static void testMiddleCIsC3Naming()
{
    std::cout << "testMiddleCIsC3Naming\n";
    CHECK_EQ(noteNameToMidi("C3", false).value_or(-1), 60);
    CHECK_EQ(noteNameToMidi("C4", false).value_or(-1), 72);
    CHECK_EQ(noteNameToMidi("A3", false).value_or(-1), 69);
    CHECK_EQ(noteNameToMidi("C-2", false).value_or(-1), 0);
    CHECK_EQ(noteNameToMidi("C1", false).value_or(-1), 36);
    CHECK_EQ(noteNameToMidi("C4").value_or(-1), 60);           // default unchanged
    CHECK(!noteNameToMidi("G9", false).has_value());            // 139 → out of range
    CHECK_EQ(midiToNoteName(60, false), std::string("C3"));
    CHECK_EQ(midiToNoteName(36, false), std::string("C1"));
    CHECK_EQ(midiToNoteName(0, false), std::string("C-2"));
    CHECK_EQ(midiToNoteName(127, false), std::string("G8"));
    CHECK_EQ(midiToNoteName(60), std::string("C4"));

    CHECK_EQ(parseFilenameTokens("Piano_C3_v64.wav", false).midiNote.value_or(-1), 60);
    CHECK_EQ(parseFilenameTokens("Piano_C3_v64.wav").midiNote.value_or(-1), 48);

    AutoMapOptions opt;
    opt.middleCIsC4 = false;
    PitchAnalysisMap a;
    a["m"] = fakePitch(64, 0.0f, 0.95f);   // MIDI 64 = E3 in C3=60 naming
    const auto r = AutoMapper::map({ makeSample("p", "Piano_C3.wav"), makeSample("m", "Lead_C3.wav"),
                                     makeSample("k", "Kick.wav") }, opt, &a);
    CHECK(!r.middleCIsC4);
    CHECK_EQ(rootOf(r, "p"), 60);
    CHECK_EQ(rootOf(r, "m"), 60);
    CHECK_EQ(rootOf(r, "k"), 36);
    const auto* p = findReview(r, "p");
    CHECK(p && describePitch(*p, r.middleCIsC4) == "Filename C3");
    const auto* m = findReview(r, "m");
    CHECK(m && m->mismatch && m->mismatch->text == "Filename C3, audio sounds E3");
    const auto* k = findReview(r, "k");
    CHECK(k && describePitch(*k, false) == "No pitch \xE2\x86\x92 C1");
    CHECK(k && describePitch(*k, true) == "No pitch \xE2\x86\x92 C2");
}

static void testPatchMetadataRoundTrip()
{
    std::cout << "testPatchMetadataRoundTrip\n";
    // Build refs + metadata the way ImportController does
    std::vector<SampleRef> refs = {
        makeSample("f", "/kit/Lead_C4.wav"),
        makeSample("d", "/kit/Pluck.wav"),
        makeSample("u", "/kit/Kick.wav"),
        makeSample("x", "/kit/Untouched.wav"),
    };
    PitchAnalysisMap a;
    a["f"] = fakePitch(64, -12.0f, 0.93f);
    a["d"] = fakePitch(50, 8.0f, 0.88f);
    a["u"] = analyse(synth::noiseHit(44100.0, 0.4));
    auto mapRefs = std::vector<SampleRef>(refs.begin(), refs.begin() + 3);
    const auto result = AutoMapper::map(mapRefs, {}, &a);
    applyPitchMetadata(mapRefs, result);
    mapRefs.push_back(refs[3]); // never mapped → no metadata (old-style ref)

    CHECK(mapRefs[0].pitchSource == PitchSource::Filename);
    CHECK(mapRefs[0].pitchMismatch);
    CHECK_EQ(mapRefs[0].detectedRootKey.value_or(-1), 64);
    CHECK(mapRefs[0].detectedCents && std::abs(*mapRefs[0].detectedCents + 12.0f) < 1e-4f);
    CHECK(mapRefs[0].pitchConfidence && std::abs(*mapRefs[0].pitchConfidence - 0.93f) < 1e-4f);
    CHECK(mapRefs[0].detectedPitchHz.has_value());
    CHECK(mapRefs[1].pitchSource == PitchSource::Detected);
    CHECK(!mapRefs[1].pitchMismatch);
    CHECK(mapRefs[2].pitchSource == PitchSource::Unpitched);
    CHECK(!mapRefs[2].detectedPitchHz.has_value());
    CHECK(!mapRefs[2].detectedCents.has_value());
    CHECK(mapRefs[2].pitchConfidence.has_value());

    Patch patch;
    patch.name = "Meta";
    patch.samples = mapRefs;
    patch.map = result.map;
    const auto json = PatchStore::toJson(patch);
    CHECK(json.find("\"schemaVersion\":1") != std::string::npos); // additive: schema unchanged
    CHECK(json.find("\"pitchSource\":\"filename\"") != std::string::npos);
    CHECK(json.find("\"pitchSource\":\"detected\"") != std::string::npos);
    CHECK(json.find("\"pitchSource\":\"unpitched\"") != std::string::npos);
    CHECK(json.find("\"pitchMismatch\":true") != std::string::npos);
    CHECK(json.find("\"pitchConfidence\":0.93") != std::string::npos);
    CHECK(json.find("\"detectedCents\":-12") != std::string::npos);

    const auto loaded = PatchStore::fromJson(json);
    CHECK(loaded.has_value());
    if (loaded)
    {
        CHECK_EQ(int(loaded->samples.size()), 4);
        CHECK(loaded->samples[0].pitchSource == PitchSource::Filename);
        CHECK(loaded->samples[0].pitchMismatch);
        CHECK_EQ(loaded->samples[0].detectedRootKey.value_or(-1), 64);
        CHECK(loaded->samples[1].pitchSource == PitchSource::Detected);
        CHECK(loaded->samples[1].detectedCents && std::abs(*loaded->samples[1].detectedCents - 8.0f) < 1e-4f);
        CHECK(loaded->samples[2].pitchSource == PitchSource::Unpitched);
        CHECK(!loaded->samples[3].pitchSource.has_value());
        CHECK(!loaded->samples[3].pitchMismatch);
        CHECK_EQ(PatchStore::toJson(*loaded), json); // stable round trip
    }
    // A ref without metadata serialises exactly as before (no new keys)
    Patch plain;
    plain.samples = { refs[3] };
    const auto plainJson = PatchStore::toJson(plain);
    CHECK(plainJson.find("pitchSource") == std::string::npos);
    CHECK(plainJson.find("pitchMismatch") == std::string::npos);
    CHECK(plainJson.find("pitchConfidence") == std::string::npos);
}

static void testOldPatchDefaults()
{
    std::cout << "testOldPatchDefaults\n";
    // Pre-v1.1 patch: only detectedPitchHz / detectedRootKey, no new keys
    const std::string old = R"({"schemaVersion":1,"name":"Old","patchRoot":".",)"
        R"("samples":[{"id":"a","path":"a.wav","displayName":"a.wav","detectedPitchHz":261.6,"detectedRootKey":60},)"
        R"({"id":"b","path":"b.wav","displayName":"b.wav"}],)"
        R"("map":{"zones":[{"sampleId":"a","rootKey":60,"keyLow":0,"keyHigh":127}]}})";
    const auto p = PatchStore::fromJson(old);
    CHECK(p.has_value());
    if (p)
    {
        CHECK_EQ(int(p->samples.size()), 2);
        CHECK(!p->samples[0].pitchSource.has_value());
        CHECK(!p->samples[0].pitchConfidence.has_value());
        CHECK(!p->samples[0].detectedCents.has_value());
        CHECK(!p->samples[0].pitchMismatch);
        CHECK_EQ(p->samples[0].detectedRootKey.value_or(-1), 60);
        CHECK(!p->samples[1].pitchSource.has_value());
        CHECK_EQ(int(p->map.zones.size()), 1);
    }
    // Unknown / future values are ignored, not fatal
    const std::string odd = R"({"schemaVersion":1,"samples":[{"id":"a","path":"a.wav","displayName":"a",)"
        R"("pitchSource":"telepathy","pitchConfidence":7,"pitchMismatch":"yes"}],"map":{"zones":[]}})";
    const auto q = PatchStore::fromJson(odd);
    CHECK(q.has_value());
    if (q)
    {
        CHECK(!q->samples[0].pitchSource.has_value());
        CHECK(q->samples[0].pitchConfidence && *q->samples[0].pitchConfidence == 1.0f); // clamped
        CHECK(!q->samples[0].pitchMismatch);
    }
    CHECK(PatchStore::pitchSourceFromString("spread") == PitchSource::Unpitched);
    CHECK_EQ(PatchStore::pitchSourceToString(PitchSource::Spread), std::string("unpitched"));
    CHECK_EQ(PatchStore::pitchSourceToString(PitchSource::Detected), std::string("detected"));
}

static void testSessionPrefsFields()
{
    std::cout << "testSessionPrefsFields\n";
    SessionPrefs d;
    CHECK(d.unpitchedFallback == AutoMapOptions::UnpitchedFallback::Chromatic);
    CHECK_EQ(d.unpitchedStartNote, 36);
    CHECK(d.middleCIsC4);
    auto o = d.toAutoMapOptions();
    CHECK(o.unpitchedFallback == AutoMapOptions::UnpitchedFallback::Chromatic);
    CHECK_EQ(o.unpitchedStartNote, 36);

    SessionPrefs p;
    p.unpitchedFallback = AutoMapOptions::UnpitchedFallback::FixedRoot;
    p.unpitchedStartNote = 48;
    p.middleCIsC4 = false;
    const auto json = p.toJson();
    CHECK(json.find("\"unpitchedFallback\":\"fixedRoot\"") != std::string::npos);
    CHECK(json.find("\"unpitchedStartNote\":48") != std::string::npos);
    CHECK(json.find("\"middleCIsC4\":false") != std::string::npos);
    const auto back = SessionPrefs::fromJson(json);
    CHECK(back && back->approximatelyEqual(p));
    CHECK(back && !back->toAutoMapOptions().middleCIsC4);
    CHECK(back && back->toAutoMapOptions().unpitchedStartNote == 48);

    // Old host state (no new keys) → new defaults
    const auto old = SessionPrefs::fromJson(R"({"polyphony":32,"middleCIsC4":true})");
    CHECK(old && old->unpitchedFallback == AutoMapOptions::UnpitchedFallback::Chromatic);
    CHECK(old && old->unpitchedStartNote == 36);
    // Legacy name + clamping
    const auto legacy = SessionPrefs::fromJson(R"({"unpitchedFallback":"spreadWarn","unpitchedStartNote":300})");
    CHECK(legacy && legacy->unpitchedFallback == AutoMapOptions::UnpitchedFallback::FixedRoot);
    CHECK(legacy && legacy->unpitchedStartNote == 127);
    CHECK(!unpitchedFallbackFromString("bogus").has_value());
}

int main()
{
    testNaturalSortAndGroupNames();
    testDrumKitSpreadOrdering();
    testSpreadSkipsPitchedKeys();
    testGroupedLayersShareKey();
    testLegacyFixedRootOption();
    testMismatchFlag();
    testMiddleCIsC3Naming();
    testPatchMetadataRoundTrip();
    testOldPatchDefaults();
    testSessionPrefsFields();
    std::cout << "\nPitchMappingTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
