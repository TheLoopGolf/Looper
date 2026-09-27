// PitchDetector (YIN) unit tests — synthetic signals, no JUCE.

#include "../Source/AutoMapper/PitchDetector.h"
#include "../Source/AutoMapper/FilenameTokens.h"
#include "SynthSignals.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace looper;

static int g_failed = 0;
static int g_passed = 0;
static double g_maxAbsCentsErr = 0.0;
static int g_accuracyCases = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static double centsError(double measuredHz, double trueHz)
{
    return 1200.0 * std::log2(measuredHz / trueHz);
}

static void expectPitch(const PitchAnalysis& r, double trueHz, const std::string& label,
                        double tolCents = 10.0)
{
    const double err = r.f0Hz > 0.0 ? centsError(r.f0Hz, trueHz) : 9999.0;
    std::cout << "  " << label << ": f0=" << r.f0Hz << " Hz, note=" << midiToNoteName(r.midiNote)
              << " (" << r.midiNote << "), cents=" << r.cents << ", conf=" << r.confidence
              << ", err=" << err << " ct, voiced " << r.framesVoiced << "/" << r.framesAnalysed << "\n";
    CHECK(!r.unpitched);
    CHECK(std::abs(err) <= tolCents);
    ++g_accuracyCases;
    g_maxAbsCentsErr = std::max(g_maxAbsCentsErr, std::abs(err));
}

static void testSinesAcrossRange()
{
    std::cout << "testSinesAcrossRange\n";
    // A0 .. C8 at 44.1k and 48k
    const int notes[] = { 21, 33, 36, 48, 57, 60, 69, 76, 84, 96, 108 };
    for (double sr : { 44100.0, 48000.0 })
    {
        for (int n : notes)
        {
            const double hz = synth::midiToHz(n);
            auto buf = synth::sine(hz, sr, 1.0);
            auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
            expectPitch(r, hz, midiToNoteName(n) + " sine @" + std::to_string(static_cast<int>(sr)));
            CHECK(r.midiNote == n);
            CHECK(r.confidence >= 0.9f);
        }
    }
    // High sample rate: A1 @ 96k
    {
        const double hz = synth::midiToHz(33);
        auto buf = synth::sine(hz, 96000.0, 1.0);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), 96000.0);
        expectPitch(r, hz, "A1 sine @96000");
        CHECK(r.midiNote == 33);
    }
}

static void testHarmonicRichNoOctaveErrors()
{
    std::cout << "testHarmonicRichNoOctaveErrors\n";
    const double sr = 44100.0;
    for (int n : { 28, 40, 45, 57, 64, 72, 81, 93 })
    {
        const double hz = synth::midiToHz(n);
        auto buf = synth::saw(hz, sr, 0.8);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, midiToNoteName(n) + " saw");
        CHECK(r.midiNote == n);
    }
    for (int n : { 36, 48, 60, 72 })
    {
        const double hz = synth::midiToHz(n);
        auto buf = synth::strongSecondHarmonic(hz, sr, 0.8);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, midiToNoteName(n) + " strong-2nd-harmonic");
        CHECK(r.midiNote == n);
    }
}

static void testDetunedCents()
{
    std::cout << "testDetunedCents\n";
    const double sr = 44100.0;
    struct Case { int note; double cents; };
    for (const Case c : { Case{ 57, +23.0 }, Case{ 49, -12.0 }, Case{ 72, -37.0 }, Case{ 40, +41.0 } })
    {
        const double hz = synth::midiToHz(c.note + c.cents / 100.0);
        auto buf = synth::saw(hz, sr, 0.8);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, midiToNoteName(c.note) + " detuned " + std::to_string(static_cast<int>(c.cents)) + " ct");
        CHECK(r.midiNote == c.note);
        CHECK(std::abs(r.cents - c.cents) <= 5.0);
    }
}

static void testPluckWithAttackNoise()
{
    std::cout << "testPluckWithAttackNoise\n";
    const double sr = 48000.0;
    for (int n : { 40, 52, 64, 76 })
    {
        const double hz = synth::midiToHz(n);
        auto buf = synth::pluck(hz, sr, 2.0);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, midiToNoteName(n) + " pluck");
        CHECK(r.midiNote == n);
        CHECK(r.confidence >= 0.8f);
    }
}

static void testNoiseAndSilenceUnpitched()
{
    std::cout << "testNoiseAndSilenceUnpitched\n";
    const double sr = 44100.0;
    for (uint32_t seed : { 12345u, 1u, 424242u, 99991u })
    {
        auto buf = synth::noise(sr, 1.0, 0.5, seed);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        std::cout << "  white noise (seed " << seed << "): conf=" << r.confidence << " reason=" << r.reason << "\n";
        CHECK(r.analysed);
        CHECK(r.unpitched);
        CHECK(r.confidence < 0.5f);
    }
    {
        auto buf = synth::noiseHit(sr, 0.6);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        std::cout << "  noise hit: conf=" << r.confidence << " reason=" << r.reason << "\n";
        CHECK(r.unpitched);
    }
    {
        std::vector<float> buf(44100, 0.0f);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        CHECK(r.unpitched);
        CHECK(r.reason == "silent");
        CHECK(r.confidence == 0.0f);
    }
    {
        // Pure DC offset is silence, not a pitch
        std::vector<float> buf(44100, 0.3f);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        CHECK(r.unpitched);
    }
    {
        // Null / empty / zero rate
        auto r = PitchDetector::analyzeMono(nullptr, 0, sr);
        CHECK(r.unpitched);
        std::vector<float> one(10, 0.5f);
        auto r2 = PitchDetector::analyzeMono(one.data(), one.size(), 0.0);
        CHECK(r2.unpitched);
    }
}

static void testDcOffsetAndShortBuffers()
{
    std::cout << "testDcOffsetAndShortBuffers\n";
    const double sr = 44100.0;
    {
        const double hz = synth::midiToHz(60);
        auto buf = synth::sine(hz, sr, 0.8, 0.3, 0.4); // big DC offset
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, "C4 sine + DC");
        CHECK(r.midiNote == 60);
    }
    {
        // 60 ms of A4 — shorter than attack skip + window: must still work
        const double hz = 440.0;
        auto buf = synth::sine(hz, sr, 0.06);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, "A4 60 ms");
        CHECK(r.midiNote == 69);
    }
    {
        // 20 ms of A5 — shorter than one full YIN frame at minHz: range shrinks
        const double hz = synth::midiToHz(81);
        auto buf = synth::sine(hz, sr, 0.02);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        expectPitch(r, hz, "A5 20 ms");
    }
    {
        // Tiny buffer: handled, unpitched
        auto buf = synth::sine(440.0, sr, 0.001);
        auto r = PitchDetector::analyzeMono(buf.data(), buf.size(), sr);
        CHECK(r.unpitched);
        CHECK(r.reason == "too short");
    }
}

static void testStereoMix()
{
    std::cout << "testStereoMix\n";
    const double sr = 44100.0;
    const double hz = synth::midiToHz(55);
    auto l = synth::saw(hz, sr, 0.8);
    auto r = synth::sine(hz, sr, 0.8, 0.3);
    auto st = synth::interleave(l, r);
    auto a = PitchDetector::analyzeInterleaved(st.data(), l.size(), 2, sr);
    expectPitch(a, hz, "G3 stereo");
    CHECK(a.midiNote == 55);

    // Tone only on the right channel, silence left
    std::vector<float> silent(l.size(), 0.0f);
    auto st2 = synth::interleave(silent, r);
    auto b = PitchDetector::analyzeInterleaved(st2.data(), l.size(), 2, sr);
    expectPitch(b, hz, "G3 right-only");
}

static void testPerformance()
{
    std::cout << "testPerformance\n";
    const double sr = 48000.0;
    auto l = synth::pluck(synth::midiToHz(45), sr, 3.0);
    auto st = synth::interleave(l, l);
    const int reps = 5;
    const auto t0 = std::chrono::steady_clock::now();
    PitchAnalysis r;
    for (int i = 0; i < reps; ++i)
        r = PitchDetector::analyzeInterleaved(st.data(), l.size(), 2, sr);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
    std::cout << "  3 s stereo 48k: " << ms << " ms per analysis\n";
    CHECK(r.midiNote == 45);
    CHECK(ms < 250.0); // generous: debug builds / shared CI runners
}

int main()
{
    testSinesAcrossRange();
    testHarmonicRichNoOctaveErrors();
    testDetunedCents();
    testPluckWithAttackNoise();
    testNoiseAndSilenceUnpitched();
    testDcOffsetAndShortBuffers();
    testStereoMix();
    testPerformance();

    std::cout << "\nAccuracy: " << g_accuracyCases << " pitched cases, max |error| = "
              << g_maxAbsCentsErr << " cents\n";
    std::cout << "Passed: " << g_passed << "  Failed: " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
