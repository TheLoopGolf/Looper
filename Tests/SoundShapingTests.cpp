// Sound shaping: filter types (LP12 / LP24 / BP / HP), dedicated filter envelope, key tracking,
// velocity -> amp / cutoff / attack, pitch-bend up / down, parameter smoothing, stability, and
// bit-exact backward compatibility with the v1 engine (Tests/reference/v1, frozen copy).
#include "../Source/AmpEnv/AmpEnv.h"
#include "../Source/Filter/SvfFilter.h"
#include "../Source/MidiRouter/MidiRouter.h"
#include "../Source/PatchStore/PatchStore.h"
#include "../Source/SamplePool/SamplePool.h"
#include "../Source/SoundShaping/SoundParams.h"
#include "../Source/VoiceEngine/VoiceEngine.h"
#include "reference/v1/MidiRouterV1.h"
#include "reference/v1/VoiceEngineV1.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace looper;

// --- Global allocation counter (audio-thread paths must not allocate) ----------------------
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

#define CHECK_NEAR(a, b, eps) do { \
    const double _a = (double)(a); const double _b = (double)(b); \
    if (!(std::fabs(_a - _b) <= (eps))) { \
        std::cerr << "FAIL: " << #a << " ~= " << #b << " (" << _a << " vs " << _b \
                  << ", eps " << (eps) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------
// Signals / helpers
// ------------------------------------------------------------------------------------------

/** Stereo, harmonically rich test sample (band-limited saw + detuned partials), deterministic. */
static SampleBuffer richBuffer(double hz, double seconds, double sr = 44100.0)
{
    SampleBuffer b;
    b.channels = 2;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(sr * seconds);
    b.interleaved.resize(static_cast<size_t>(b.length) * 2);
    const int nh = std::max(1, static_cast<int>((sr * 0.45) / hz));
    for (int64_t i = 0; i < b.length; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        double l = 0.0, r = 0.0;
        for (int k = 1; k <= nh; ++k)
        {
            l += std::sin(2.0 * kPi * hz * k * t) / k;
            r += std::sin(2.0 * kPi * hz * 1.003 * k * t + 0.3 * k) / k;
        }
        b.interleaved[static_cast<size_t>(i) * 2] = static_cast<float>(0.3 * l);
        b.interleaved[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(0.3 * r);
    }
    return b;
}

static SampleBuffer sineBuffer(double hz, double seconds, double sr = 44100.0, float amp = 0.5f)
{
    SampleBuffer b;
    b.channels = 1;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(sr * seconds);
    b.interleaved.resize(static_cast<size_t>(b.length));
    for (int64_t i = 0; i < b.length; ++i)
        b.interleaved[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * kPi * hz * i / sr));
    return b;
}

static Zone fullZone(const std::string& id, int root = 60, int keyLo = 0, int keyHi = 127)
{
    Zone z;
    z.sampleId = id;
    z.rootKey = root;
    z.keyLow = keyLo;
    z.keyHigh = keyHi;
    return z;
}

/** Steady-state gain of `f` at `hz` (sine in, RMS out / RMS in after settling). */
static double measureGain(SvfFilter f, double hz, double sr)
{
    f.reset();
    const int settle = static_cast<int>(sr * 0.25);
    const int measure = static_cast<int>(sr * 0.25);
    double inE = 0.0, outE = 0.0;
    for (int i = 0; i < settle + measure; ++i)
    {
        const float x = static_cast<float>(std::sin(2.0 * kPi * hz * i / sr));
        const float y = f.process(x);
        if (i >= settle)
        {
            inE += static_cast<double>(x) * x;
            outE += static_cast<double>(y) * y;
        }
    }
    return std::sqrt(outE / inE);
}

static double toDb(double g) { return 20.0 * std::log10(std::max(g, 1e-12)); }

/** Analog prototype of the trapezoidal SVF, evaluated at the pre-warped frequency (exact for TPT). */
static double expectedGain(FilterType type, double hz, double fc, double sr, float resonance)
{
    const double w = std::tan(kPi * hz / sr) / std::tan(kPi * std::min(fc, 0.49 * sr) / sr);
    const double k = 1.0 / std::max(0.1, static_cast<double>(SvfFilter::resonanceToQ(resonance)));
    auto den = [w](double kk) { return std::sqrt((1.0 - w * w) * (1.0 - w * w) + (kk * w) * (kk * w)); };
    switch (type)
    {
        case FilterType::LowPass: return 1.0 / den(k);
        case FilterType::HighPass: return (w * w) / den(k);
        case FilterType::BandPass: return w / den(k);
        case FilterType::LowPass24: return (1.0 / den(k)) * (1.0 / den(static_cast<double>(SvfFilter::kStage2K)));
    }
    return 0.0;
}

// ------------------------------------------------------------------------------------------
// Filter responses
// ------------------------------------------------------------------------------------------

static void testFilterResponsesPerType()
{
    std::cout << "testFilterResponsesPerType\n";
    const double sr = 48000.0;
    const double fc = 1000.0;
    const double freqs[] = { 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
    for (float res : { 0.0f, 0.2f, 0.7f })
    {
        for (FilterType type : { FilterType::LowPass, FilterType::LowPass24, FilterType::BandPass, FilterType::HighPass })
        {
            SvfFilter f;
            f.setSampleRate(sr);
            f.setType(type);
            f.setCutoffHz(static_cast<float>(fc));
            f.setResonance(res);
            for (double hz : freqs)
            {
                const double exp = toDb(expectedGain(type, hz, fc, sr, res));
                if (exp < -90.0)
                    continue; // below the float noise floor of the measurement
                const double got = toDb(measureGain(f, hz, sr));
                CHECK_NEAR(got, exp, 0.25);
            }
        }
    }

    // Shape checks at resonance 0 (Q 0.5): pass band, slope per octave, band-pass centre
    auto gainDb = [&](FilterType t, double hz) {
        SvfFilter f;
        f.setSampleRate(sr);
        f.setType(t);
        f.setCutoffHz(static_cast<float>(fc));
        f.setResonance(0.0f);
        return toDb(measureGain(f, hz, sr));
    };
    CHECK_NEAR(gainDb(FilterType::LowPass, 62.5), 0.0, 0.1);
    CHECK_NEAR(gainDb(FilterType::LowPass24, 62.5), 0.0, 0.1);
    CHECK_NEAR(gainDb(FilterType::HighPass, 16000.0), 0.0, 0.3);
    // Slopes well above / below the cutoff: ~12 dB/oct (12 dB types), ~24 dB/oct (LP24)
    const double lp12Slope = gainDb(FilterType::LowPass, 4000.0) - gainDb(FilterType::LowPass, 8000.0);
    const double lp24Slope = gainDb(FilterType::LowPass24, 4000.0) - gainDb(FilterType::LowPass24, 8000.0);
    const double hpSlope = gainDb(FilterType::HighPass, 125.0) - gainDb(FilterType::HighPass, 62.5);
    std::cout << "  slopes dB/oct: LP12 " << lp12Slope << "  LP24 " << lp24Slope << "  HP " << hpSlope << "\n";
    CHECK(lp12Slope > 10.5 && lp12Slope < 14.5);
    CHECK(lp24Slope > 21.0 && lp24Slope < 29.0);
    CHECK(hpSlope > 10.5 && hpSlope < 13.0);
    // LP24 is much steeper than LP12 two octaves above the cutoff
    CHECK(gainDb(FilterType::LowPass24, 4000.0) < gainDb(FilterType::LowPass, 4000.0) - 18.0);
    // Band-pass peaks at the cutoff (gain Q there) and falls off both sides (6 dB/oct skirts)
    const double bpC = gainDb(FilterType::BandPass, 1000.0);
    CHECK_NEAR(bpC, toDb(SvfFilter::resonanceToQ(0.0f)), 0.1);
    CHECK(gainDb(FilterType::BandPass, 125.0) < bpC - 9.0);
    CHECK(gainDb(FilterType::BandPass, 8000.0) < bpC - 9.0);
    CHECK(gainDb(FilterType::BandPass, 500.0) < bpC && gainDb(FilterType::BandPass, 2000.0) < bpC);
}

static void testTwelveDbTypesBitIdenticalToV1()
{
    std::cout << "testTwelveDbTypesBitIdenticalToV1\n";
    // The v1 filter types must still run exactly the v1 arithmetic.
    uint32_t seed = 777u;
    auto noise = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFFFF) / 8388607.5f - 1.0f;
    };
    const std::pair<FilterType, looper_v1::FilterType> types[] = {
        { FilterType::LowPass, looper_v1::FilterType::LowPass },
        { FilterType::HighPass, looper_v1::FilterType::HighPass },
        { FilterType::BandPass, looper_v1::FilterType::BandPass },
    };
    bool identical = true;
    for (const auto& [now, old] : types)
    {
        SvfFilter a;
        looper_v1::SvfFilter b;
        a.setSampleRate(44100.0);
        b.setSampleRate(44100.0);
        a.setType(now);
        b.setType(old);
        for (int i = 0; i < 20000; ++i)
        {
            if (i % 997 == 0)
            {
                const float fc = 40.0f + 15000.0f * (0.5f + 0.5f * noise());
                const float r = 0.5f + 0.5f * noise();
                a.setCutoffHz(fc);
                b.setCutoffHz(fc);
                a.setResonance(r);
                b.setResonance(r);
            }
            float l1 = noise(), r1 = noise();
            float l2 = l1, r2 = r1;
            a.processStereo(l1, r1);
            b.processStereo(l2, r2);
            identical = identical && std::memcmp(&l1, &l2, sizeof(float)) == 0 && std::memcmp(&r1, &r2, sizeof(float)) == 0;
        }
    }
    CHECK(identical);
}

// ------------------------------------------------------------------------------------------
// Engine fixture
// ------------------------------------------------------------------------------------------

struct Rig
{
    SamplePool pool;
    VoiceEngine engine;
    MidiRouter router;
    std::shared_ptr<InstrumentMap> map = std::make_shared<InstrumentMap>();
    std::vector<float> l, r;
    double sr = 48000.0;

    explicit Rig(double sampleRate = 48000.0, SampleBuffer sample = sineBuffer(220.0, 3.0), VelCurve curve = VelCurve::Linear)
        : sr(sampleRate)
    {
        pool.setBuffer("s", std::move(sample));
        map->zones.push_back(fullZone("s"));
        map->velCurve = curve;
        engine.setSamplePool(&pool);
        engine.setSampleRate(sr);
        engine.adoptMap(map);
        router.setEngine(&engine);
        l.assign(VoiceEngine::kSubBlock * 8, 0.0f);
        r.assign(l.size(), 0.0f);
    }

    void apply(const sound::SoundParams& p) { sound::applyToEngine(p, engine, router); }

    void run(int samples, const sound::SoundParams* p = nullptr, int block = 64)
    {
        while (samples > 0)
        {
            const int n = std::min(block, samples);
            if (p != nullptr)
                apply(*p);
            engine.processBlock(l.data(), r.data(), n);
            samples -= n;
        }
    }

    const Voice* voice(int note, int channel = 1) const { return engine.findActiveVoice(note, channel); }
};

static sound::SoundParams baseParams()
{
    sound::SoundParams p;
    p.amp = { 1.0f, 100.0f, 1.0f, 50.0f };
    p.filter.type = FilterType::LowPass;
    p.filter.cutoffHz = 500.0f;
    p.filter.resonance = 0.2f;
    return p;
}

static void testFilterEnvModulatesCutoff()
{
    std::cout << "testFilterEnvModulatesCutoff\n";
    Rig rig;
    auto p = baseParams();
    p.filter.fenvAmountSemis = 24.0f;              // +2 octaves at full envelope
    p.filterEnv = { 10.0f, 50.0f, 0.5f, 100.0f };
    rig.apply(p);
    rig.router.handleNoteOn(60, 127, 1);

    // During the attack the cutoff follows 500 * 2^(2 * env)
    rig.run(240, &p, 16); // 5 ms
    const Voice* v = rig.voice(60);
    CHECK(v != nullptr);
    if (v == nullptr)
        return;
    const float lvlAttack = v->filterEnv.level();
    CHECK(v->filterEnv.stage() == AmpEnv::Stage::Attack);
    CHECK(lvlAttack > 0.4f && lvlAttack < 0.6f);
    CHECK_NEAR(v->filter.cutoffHz(), 500.0 * std::exp2(2.0 * lvlAttack), 0.5);

    // Peak: 10 ms -> full envelope = 2000 Hz
    rig.run(240, &p, 16);
    CHECK_NEAR(v->filter.cutoffHz(), 500.0 * std::exp2(2.0 * v->filterEnv.level()), 0.5);
    float peak = 0.0f;
    for (int i = 0; i < 48; ++i)
    {
        rig.run(1, &p, 1);
        peak = std::max(peak, v->filter.cutoffHz());
    }
    CHECK_NEAR(peak, 2000.0, 20.0);

    // Sustain 0.5 -> one octave up
    rig.run(4800, &p, 64);
    CHECK(v->filterEnv.stage() == AmpEnv::Stage::Sustain);
    CHECK_NEAR(v->filter.cutoffHz(), 1000.0, 0.01);

    // The amp envelope is independent (sustain 1.0 here)
    CHECK_NEAR(v->ampEnv.level(), 1.0, 1e-6);

    // Release: back toward the knob cutoff
    rig.router.handleNoteOff(60, 1);
    rig.run(2400, &p, 64); // 50 ms of a 100 ms release
    CHECK(v->filter.cutoffHz() < 1000.0f && v->filter.cutoffHz() > 500.0f);
    CHECK_NEAR(v->filter.cutoffHz(), 500.0 * std::exp2(2.0 * v->filterEnv.level()), 0.5);

    // Negative amount closes the filter: -12 st at peak = 250 Hz
    Rig neg;
    auto q = baseParams();
    q.filter.fenvAmountSemis = -12.0f;
    q.filterEnv = { 5.0f, 2000.0f, 0.0f, 100.0f };
    neg.apply(q);
    neg.router.handleNoteOn(60, 100, 1);
    float lowest = 1.0e9f;
    for (int i = 0; i < 480; ++i)
    {
        neg.run(1, &q, 1);
        if (const Voice* nv = neg.voice(60))
            lowest = std::min(lowest, nv->filter.cutoffHz());
    }
    CHECK_NEAR(lowest, 250.0, 3.0);

    // Amount 0: the envelope runs but does not touch the cutoff
    Rig off;
    auto z = baseParams();
    z.filterEnv = { 5.0f, 20.0f, 0.3f, 100.0f };
    off.apply(z);
    off.router.handleNoteOn(60, 100, 1);
    off.run(2000, &z, 64);
    if (const Voice* ov = off.voice(60))
        CHECK(ov->filter.cutoffHz() == 500.0f);
}

static void testKeyTrackingMath()
{
    std::cout << "testKeyTrackingMath\n";
    struct Case { float track; int note; double expectHz; };
    const Case cases[] = {
        { 1.0f, 60, 1000.0 }, { 1.0f, 72, 2000.0 }, { 1.0f, 48, 500.0 }, { 1.0f, 84, 4000.0 },
        { 0.5f, 84, 2000.0 }, { 0.5f, 36, 500.0 }, { 0.25f, 108, 2000.0 }, { 0.0f, 96, 1000.0 },
        { 1.0f, 67, 1000.0 * std::exp2(7.0 / 12.0) },
    };
    for (const auto& c : cases)
    {
        Rig rig;
        auto p = baseParams();
        p.filter.cutoffHz = 1000.0f;
        p.filter.keyTrack = c.track;
        rig.apply(p);
        rig.router.handleNoteOn(c.note, 100, 1);
        rig.run(256, &p);
        const Voice* v = rig.voice(c.note);
        CHECK(v != nullptr);
        if (v != nullptr)
        {
            CHECK_NEAR(v->keyOct, (c.note - 60) / 12.0, 1e-6);
            CHECK_NEAR(v->filter.cutoffHz(), c.expectHz, c.expectHz * 1e-5);
        }
    }
    // Key tracking adds to the envelope in octaves: note 72, 100 %, +12 st env at sustain 1 -> x4
    Rig rig;
    auto p = baseParams();
    p.filter.cutoffHz = 1000.0f;
    p.filter.keyTrack = 1.0f;
    p.filter.fenvAmountSemis = 12.0f;
    p.filterEnv = { 0.1f, 1.0f, 1.0f, 50.0f };
    rig.apply(p);
    rig.router.handleNoteOn(72, 100, 1);
    rig.run(512, &p);
    if (const Voice* v = rig.voice(72))
        CHECK_NEAR(v->filter.cutoffHz(), 4000.0, 0.05);
}

static void testVelocityCurves()
{
    std::cout << "testVelocityCurves\n";
    // velocity -> amp: amount 100 % equals the v1 gain bit-for-bit for every curve and velocity
    for (VelCurve curve : { VelCurve::Linear, VelCurve::Soft, VelCurve::Hard })
    {
        Rig rig(48000.0, sineBuffer(220.0, 0.5), curve);
        looper_v1::VoiceEngine old;
        old.setSamplePool(&rig.pool);
        old.setSampleRate(48000.0);
        old.adoptMap(rig.map);
        bool exact = true, monotonic = true;
        float prevFull = -1.0f;
        for (int vel = 1; vel <= 127; ++vel)
        {
            old.noteOn(60, vel, 1);
            const float v1Gain = old.findActiveVoice(60, 1)->velocityAmp;
            old.allNotesOff();
            const float full = rig.engine.velocityGain(vel);
            exact = exact && std::memcmp(&full, &v1Gain, sizeof(float)) == 0;
            monotonic = monotonic && full >= prevFull;
            prevFull = full;

            // Partial amounts blend toward "no velocity" linearly
            for (float amt : { 0.0f, 0.25f, 0.5f, 0.75f })
            {
                VelocityParams vp;
                vp.toAmp = amt;
                rig.engine.setVelocityParams(vp);
                const double expect = 1.0 - amt * (1.0 - static_cast<double>(v1Gain));
                CHECK_NEAR(rig.engine.velocityGain(vel), expect, 1e-6);
            }
            rig.engine.setVelocityParams({});
        }
        CHECK(exact);
        CHECK(monotonic);
        CHECK_NEAR(rig.engine.velocityGain(127), 1.0, 0.0);
    }
    {
        // Curve shapes at velocity 64 (0.504): soft > linear > hard
        Rig lin(48000.0, sineBuffer(220.0, 0.5), VelCurve::Linear);
        Rig soft(48000.0, sineBuffer(220.0, 0.5), VelCurve::Soft);
        Rig hard(48000.0, sineBuffer(220.0, 0.5), VelCurve::Hard);
        const double x = 64.0 / 127.0;
        CHECK_NEAR(lin.engine.velocityGain(64), x, 1e-6);
        CHECK_NEAR(soft.engine.velocityGain(64), std::sqrt(x), 1e-6);
        CHECK_NEAR(hard.engine.velocityGain(64), x * x, 1e-6);
    }
    {
        // The rendered level follows the gain: amount 0 -> velocity 20 as loud as 127
        auto peakFor = [](int vel, float amt) {
            Rig rig(48000.0, sineBuffer(220.0, 0.5));
            auto p = baseParams();
            p.filter.cutoffHz = 20000.0f;
            p.velocity.toAmp = amt;
            rig.apply(p);
            rig.router.handleNoteOn(60, vel, 1);
            float peak = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                rig.run(256, &p, 256);
                for (int i = 0; i < 256; ++i)
                    peak = std::max(peak, std::fabs(rig.l[static_cast<size_t>(i)]));
            }
            return peak;
        };
        const float p127 = peakFor(127, 1.0f);
        CHECK_NEAR(peakFor(20, 0.0f) / p127, 1.0, 0.01);
        CHECK_NEAR(peakFor(20, 1.0f) / p127, 20.0 / 127.0, 0.01);
        CHECK_NEAR(peakFor(20, 0.5f) / p127, 1.0 - 0.5 * (1.0 - 20.0 / 127.0), 0.01);
    }
    {
        // velocity -> cutoff: no offset at 127, -amount * (1 - vel) below
        for (int vel : { 127, 100, 64, 10 })
        {
            Rig rig;
            auto p = baseParams();
            p.filter.cutoffHz = 4000.0f;
            p.filter.velToCutoffSemis = 24.0f;
            rig.apply(p);
            rig.router.handleNoteOn(60, vel, 1);
            rig.run(256, &p);
            const double c = vel / 127.0;
            if (const Voice* v = rig.voice(60))
                CHECK_NEAR(v->filter.cutoffHz(), 4000.0 * std::exp2(2.0 * (c - 1.0)), 0.05);
        }
        // negative amount: softer notes brighter than the knob (clamped below Nyquist)
        Rig rig;
        auto p = baseParams();
        p.filter.cutoffHz = 1000.0f;
        p.filter.velToCutoffSemis = -12.0f;
        rig.apply(p);
        rig.router.handleNoteOn(60, 1, 1);
        rig.run(256, &p);
        if (const Voice* v = rig.voice(60))
            CHECK(v->filter.cutoffHz() > 1900.0f);
    }
    {
        // velocity -> attack: knob value at 127, up to 16x longer at the softest, monotonic
        Rig rig;
        auto p = baseParams();
        p.amp.attackMs = 10.0f;
        p.velocity.toAttack = 1.0f;
        rig.apply(p);
        CHECK_NEAR(rig.engine.velocityAttackMs(127), 10.0, 1e-5);
        CHECK_NEAR(rig.engine.velocityAttackMs(1), 10.0 * std::exp2(4.0 * (1.0 - 1.0 / 127.0)), 1e-3);
        CHECK(rig.engine.velocityAttackMs(64) > 10.0f && rig.engine.velocityAttackMs(64) < rig.engine.velocityAttackMs(32));
        p.velocity.toAttack = 0.5f;
        rig.apply(p);
        CHECK_NEAR(rig.engine.velocityAttackMs(1), 10.0 * std::exp2(2.0 * (1.0 - 1.0 / 127.0)), 1e-3);
        p.velocity.toAttack = 0.0f;
        rig.apply(p);
        CHECK(rig.engine.velocityAttackMs(1) == 10.0f);

        // Measured: samples until the amp envelope reaches 1.0 (harder = faster)
        auto attackSamples = [](int vel) {
            Rig r;
            auto q = baseParams();
            q.amp.attackMs = 10.0f;
            q.velocity.toAttack = 1.0f;
            r.apply(q);
            r.router.handleNoteOn(60, vel, 1);
            int n = 0;
            for (; n < 48000; ++n)
            {
                r.run(1, &q, 1);
                const Voice* v = r.voice(60);
                if (v == nullptr || v->ampEnv.stage() != AmpEnv::Stage::Attack)
                    break;
            }
            return n;
        };
        const int hard = attackSamples(127);
        const int mid = attackSamples(64);
        const int soft = attackSamples(16);
        std::cout << "  attack samples: v127 " << hard << "  v64 " << mid << "  v16 " << soft << "\n";
        CHECK(std::abs(hard - 480) <= 2);
        CHECK(mid > hard && soft > mid);
        CHECK(std::abs(soft - static_cast<int>(480.0 * std::exp2(4.0 * (1.0 - 16.0 / 127.0)))) <= 3);
    }
}

static void testPitchBendUpDown()
{
    std::cout << "testPitchBendUpDown\n";
    Rig rig;
    rig.router.handleNoteOn(60, 100, 1);
    // v1 default 2 / 2 is exactly the v1 router
    looper_v1::VoiceEngine oldEngine;
    looper_v1::MidiRouter oldRouter;
    oldRouter.setEngine(&oldEngine);
    bool same = true;
    for (int value : { 0, 1, 4096, 8191, 8192, 8193, 12000, 16383 })
    {
        rig.router.handlePitchBend(value, 1);
        oldRouter.handlePitchBend(value, 1);
        const float a = rig.engine.pitchBendSemis(), b = oldEngine.pitchBendSemis();
        same = same && std::memcmp(&a, &b, sizeof(float)) == 0;
    }
    CHECK(same);

    rig.router.setBendRange(12.0f, 2.0f);
    rig.router.handlePitchBend(16383, 1);
    CHECK_NEAR(rig.engine.pitchBendSemis(), 12.0 * 8191.0 / 8192.0, 1e-5);
    rig.router.handlePitchBend(0, 1);
    CHECK_NEAR(rig.engine.pitchBendSemis(), -2.0, 1e-6);
    rig.router.handlePitchBend(8192, 1);
    CHECK(rig.engine.pitchBendSemis() == 0.0f);
    // A held bend follows a range change
    rig.router.handlePitchBend(0, 1);
    rig.router.setBendRange(12.0f, 24.0f);
    CHECK_NEAR(rig.engine.pitchBendSemis(), -24.0, 1e-6);
    // The sounding voice is re-pitched: 2^(-24/12) = 0.25
    if (const Voice* v = rig.voice(60))
        CHECK_NEAR(v->pitchRatio, 0.25, 1e-9);
    // Zero range = bend off in that direction
    rig.router.setBendRange(0.0f, 0.0f);
    CHECK(rig.engine.pitchBendSemis() == 0.0f);

    // Through SoundParams (what the plugin pushes every block)
    auto p = baseParams();
    p.bendUpSemis = 7.0f;
    p.bendDownSemis = 5.0f;
    rig.apply(p);
    rig.router.handlePitchBend(16383, 1);
    CHECK_NEAR(rig.engine.pitchBendSemis(), 7.0 * 8191.0 / 8192.0, 1e-5);
}

// ------------------------------------------------------------------------------------------
// Smoothing
// ------------------------------------------------------------------------------------------

/** Largest |second difference| of a signal: a click / zipper shows up as a spike. */
static double maxSecondDiff(const std::vector<float>& x, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t>(from, 2); i < std::min(to, x.size()); ++i)
        m = std::max(m, std::fabs(static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
    return m;
}

static void testParamSmoothingWithoutDiscontinuities()
{
    std::cout << "testParamSmoothingWithoutDiscontinuities\n";
    const double sr = 48000.0;
    const int rampN = ParamRamp::samplesFor(sr);
    CHECK(rampN == 960);

    // 1) Cutoff jump while a note sounds: per-sample cutoff glides in log steps and lands exactly
    {
        Rig rig(sr, sineBuffer(220.0, 2.0));
        auto p = baseParams();
        p.filter.cutoffHz = 200.0f;
        rig.apply(p);
        rig.router.handleNoteOn(60, 100, 1);
        rig.run(4800, &p);
        const Voice* v = rig.voice(60);
        CHECK(v != nullptr);
        if (v == nullptr)
            return;
        CHECK(v->filter.cutoffHz() == 200.0f);
        p.filter.cutoffHz = 8000.0f;
        rig.apply(p);
        CHECK(rig.engine.filterSmoothing());
        double prev = 200.0, maxStepOct = 0.0;
        bool monotonic = true;
        int samples = 0;
        while (rig.engine.filterSmoothing() && samples < 10000)
        {
            rig.engine.processBlock(rig.l.data(), rig.r.data(), 1);
            const double c = v->filter.cutoffHz();
            monotonic = monotonic && c >= prev;
            maxStepOct = std::max(maxStepOct, std::log2(c / prev));
            prev = c;
            ++samples;
        }
        CHECK(monotonic);
        CHECK(samples == rampN);
        CHECK(maxStepOct <= std::log2(40.0) / rampN * 1.01);
        rig.engine.processBlock(rig.l.data(), rig.r.data(), 1);
        CHECK(v->filter.cutoffHz() == 8000.0f); // settled: exact knob value
    }

    // 2) Audible: a cutoff / resonance jump renders without the click v1 makes
    auto render = [&](bool v1) {
        SamplePool pool;
        pool.setBuffer("s", sineBuffer(110.0, 2.0, 48000.0, 0.8f));
        auto map = std::make_shared<InstrumentMap>();
        map->zones.push_back(fullZone("s"));
        std::vector<float> out;
        std::vector<float> l(128), r(128);
        looper_v1::VoiceEngine old;
        VoiceEngine now;
        auto setF = [&](float cut, float res) {
            if (v1)
            {
                looper_v1::FilterParams f;
                f.cutoffHz = cut;
                f.resonance = res;
                old.setFilterParams(f);
            }
            else
            {
                FilterParams f;
                f.cutoffHz = cut;
                f.resonance = res;
                now.setFilterParams(f);
            }
        };
        if (v1) { old.setSamplePool(&pool); old.setSampleRate(48000.0); old.adoptMap(map); old.setEnvParams({ 1.0f, 10.0f, 1.0f, 50.0f }); }
        else    { now.setSamplePool(&pool); now.setSampleRate(48000.0); now.adoptMap(map); now.setEnvParams({ 1.0f, 10.0f, 1.0f, 50.0f }); }
        setF(150.0f, 0.9f);
        if (v1) old.noteOn(60, 127, 1); else now.noteOn(60, 127, 1);
        for (int b = 0; b < 300; ++b)
        {
            if (b == 150)
                setF(6000.0f, 0.0f);
            if (v1) old.processBlock(l.data(), r.data(), 128); else now.processBlock(l.data(), r.data(), 128);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto a = render(false);
    const auto b = render(true);
    const size_t change = 150 * 128;
    const double steady = maxSecondDiff(a, change + 4000, a.size());   // after everything settled
    const double smoothed = maxSecondDiff(a, change, change + 2000);
    const double stepped = maxSecondDiff(b, change, change + 2000);
    std::cout << "  max |d2|: steady " << steady << "  smoothed change " << smoothed << "  v1 step " << stepped << "\n";
    // The swept filter rings a little while it glides (real signal, not a click); the v1 coefficient
    // step is a hard discontinuity two orders of magnitude larger.
    CHECK(smoothed < steady * 12.0);
    CHECK(stepped > smoothed * 50.0);

    // 3) Filter-env amount and key-track changes mid-note glide too (no jump in one sample)
    {
        Rig rig(sr, sineBuffer(220.0, 2.0));
        auto p = baseParams();
        p.filter.cutoffHz = 400.0f;
        p.filterEnv = { 0.1f, 1.0f, 1.0f, 50.0f }; // env sits at 1.0
        rig.apply(p);
        rig.router.handleNoteOn(72, 100, 1);
        rig.run(2400, &p);
        const Voice* v = rig.voice(72);
        if (v == nullptr) { CHECK(false); return; }
        p.filter.fenvAmountSemis = 36.0f;
        p.filter.keyTrack = 1.0f;
        rig.apply(p);
        double prev = v->filter.cutoffHz(), maxStep = 0.0;
        for (int i = 0; i < rampN + 10; ++i)
        {
            rig.engine.processBlock(rig.l.data(), rig.r.data(), 1);
            maxStep = std::max(maxStep, std::fabs(std::log2(v->filter.cutoffHz() / prev)));
            prev = v->filter.cutoffHz();
        }
        CHECK(maxStep < 4.0 / rampN * 1.05);          // 4 octaves over the ramp
        CHECK_NEAR(v->filter.cutoffHz(), 400.0 * 16.0, 0.1);
        // ... and back to 0: lands exactly on the static knob value again
        p.filter.fenvAmountSemis = 0.0f;
        p.filter.keyTrack = 0.0f;
        rig.apply(p);
        rig.run(rampN + 256, &p, 64);
        CHECK(!rig.engine.filterSmoothing());
        CHECK(v->filter.cutoffHz() == 400.0f);
    }

    // 4) Silent engine: changes apply at once (a patch load never sweeps)
    {
        Rig rig;
        auto p = baseParams();
        rig.apply(p);
        p.filter.cutoffHz = 9000.0f;
        rig.apply(p);
        CHECK(!rig.engine.filterSmoothing());
    }
}

// ------------------------------------------------------------------------------------------
// Stability / denormals / real-time safety
// ------------------------------------------------------------------------------------------

static bool isSubnormal(float x) { return x != 0.0f && std::fabs(x) < 1.17549435e-38f; }

static void testStabilityAtExtremeSettings()
{
    std::cout << "testStabilityAtExtremeSettings\n";
    for (double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
    {
        for (FilterType type : { FilterType::LowPass, FilterType::LowPass24, FilterType::BandPass, FilterType::HighPass })
        {
            for (float semis : { 60.0f, -60.0f })
            {
                Rig rig(sr, richBuffer(55.0, 1.0));
                auto p = baseParams();
                p.filter.type = type;
                p.filter.cutoffHz = semis > 0 ? 20000.0f : 20.0f;
                p.filter.resonance = 1.0f;
                p.filter.envAmount = semis > 0 ? 1.0f : -1.0f;  // legacy amp-env mod on top
                p.filter.fenvAmountSemis = semis;
                p.filter.keyTrack = 1.0f;
                p.filter.velToCutoffSemis = -semis;
                p.filterEnv = { 0.1f, 5.0f, 0.5f, 5.0f };
                p.velocity = { 1.0f, 1.0f };
                p.amp = { 0.1f, 5.0f, 1.0f, 30.0f };
                rig.apply(p);
                for (int note : { 0, 60, 127 })
                    rig.router.handleNoteOn(note, note == 0 ? 1 : 127, 1);
                rig.router.handleCc(1, 127, 1);                // mod wheel +2 oct
                rig.router.setBendRange(48.0f, 48.0f);
                rig.router.handlePitchBend(16383, 1);
                bool finite = true;
                float peak = 0.0f;
                for (int b = 0; b < 60; ++b)
                {
                    // wiggle the cutoff every block (smoothing + modulation at once)
                    p.filter.cutoffHz = (b % 2) ? 20.0f : 20000.0f;
                    if (b == 30)
                        p.filter.type = type == FilterType::LowPass ? FilterType::LowPass24 : FilterType::LowPass;
                    rig.run(256, &p, 256);
                    for (int i = 0; i < 256; ++i)
                    {
                        finite = finite && std::isfinite(rig.l[static_cast<size_t>(i)]) && std::isfinite(rig.r[static_cast<size_t>(i)]);
                        peak = std::max(peak, std::fabs(rig.l[static_cast<size_t>(i)]));
                    }
                }
                CHECK(finite);
                CHECK(peak < 50.0f);
                for (int note : { 0, 60, 127 })
                    if (const Voice* v = rig.voice(note))
                        CHECK(v->filter.isFinite());

                // Release everything; the tail decays to exact silence (no denormals left)
                rig.router.handleCc(1, 0, 1);
                rig.engine.allNotesOff();
                bool subnormal = false;
                for (int b = 0; b < 40; ++b)
                {
                    rig.run(256, &p, 256);
                    for (int i = 0; i < 256; ++i)
                        subnormal = subnormal || isSubnormal(rig.l[static_cast<size_t>(i)]) || isSubnormal(rig.r[static_cast<size_t>(i)]);
                }
                CHECK(!subnormal);
                CHECK(rig.engine.activeVoiceCount() == 0);
            }
        }
    }

    // A filter fed silence after loud input flushes its state to exact zero (both LP24 stages)
    for (FilterType type : { FilterType::LowPass, FilterType::LowPass24, FilterType::BandPass, FilterType::HighPass })
    {
        SvfFilter f;
        f.setSampleRate(48000.0);
        f.setType(type);
        f.setCutoffHz(30.0f);
        f.setResonance(1.0f);
        for (int i = 0; i < 4800; ++i)
            f.process((i / 24) % 2 ? 1.0f : -1.0f);
        bool sub = false;
        float y = 1.0f;
        for (int i = 0; i < 48000 * 20; ++i)
        {
            y = f.process(0.0f);
            sub = sub || isSubnormal(y);
        }
        CHECK(!sub);
        CHECK(y == 0.0f);
    }
}

static void testRealtimeNoAllocation()
{
    std::cout << "testRealtimeNoAllocation\n";
    Rig rig(48000.0, richBuffer(110.0, 1.0));
    rig.engine.setPolyphony(16);
    auto p = baseParams();
    p.filter.type = FilterType::LowPass24;
    p.filter.fenvAmountSemis = 30.0f;
    p.filter.keyTrack = 0.5f;
    p.filter.velToCutoffSemis = 12.0f;
    p.velocity = { 0.6f, 0.8f };
    rig.apply(p);
    rig.run(512, &p);
    const long before = g_allocs.load();
    for (int b = 0; b < 200; ++b)
    {
        if (b % 10 == 0)
            rig.router.handleNoteOn(40 + b % 40, 20 + b % 100, 1);
        if (b % 10 == 5)
            rig.router.handleNoteOff(40 + (b - 5) % 40, 1);
        p.filter.cutoffHz = 300.0f + 50.0f * static_cast<float>(b % 20);
        p.filter.resonance = 0.05f * static_cast<float>(b % 20);
        p.filter.fenvAmountSemis = static_cast<float>(b % 7) * 5.0f;
        rig.router.handlePitchBend(b * 80 % 16384, 1);
        rig.run(256, &p, 256);
    }
    CHECK(g_allocs.load() == before);
}

// ------------------------------------------------------------------------------------------
// Backward compatibility: an old patch renders bit-identically to v1
// ------------------------------------------------------------------------------------------

/** A v1 .looper.json (schema 1, nine sound params, no sound-shaping fields). */
static std::string oldPatchJson(const char* filterType, const char* cutoff, const char* resonance,
                                const char* envAmt, const char* velCurve)
{
    std::string j = R"({"schemaVersion":1,"name":"Old","patchRoot":".","samples":[)"
        R"({"id":"lo","path":"mem://lo","displayName":"lo.wav","durationSamples":88200,"sampleRate":44100,"channels":2},)"
        R"({"id":"hi","path":"mem://hi","displayName":"hi.wav","durationSamples":88200,"sampleRate":44100,"channels":2}],)"
        R"("map":{"volumeDb":0,"polyphonyLimit":8,"glideMs":0,"velCurve":"VELCURVE","modWheelTarget":"FilterCutoff","roundRobinMode":"cycle","loadIntoRam":false,"zones":[)"
        R"({"sampleId":"lo","rootKey":48,"keyLow":0,"keyHigh":59,"velLow":1,"velHigh":127,"rrGroup":0,"rrIndex":0,"tuneCents":-7,"coarseTranspose":0,"gainDb":-1.5,"pan":-0.3},)"
        R"({"sampleId":"hi","rootKey":67,"keyLow":60,"keyHigh":127,"velLow":1,"velHigh":127,"rrGroup":0,"rrIndex":0,"tuneCents":12,"coarseTranspose":0,"gainDb":0,"pan":0.4}]},)"
        R"("params":{"attack":4.99999952316284,"cutoff":CUTOFF,"decay":180.000015258789,"filterEnvAmt":ENVAMT,)"
        R"("filterType":FTYPE,"release":250,"resonance":RES,"sustain":0.649999976158142,"volume":-3.20000004768372}})";
    auto put = [&j](const std::string& key, const std::string& value) {
        const auto at = j.find(key);
        j.replace(at, key.size(), value);
    };
    put("VELCURVE", velCurve);
    put("CUTOFF", cutoff);
    put("ENVAMT", envAmt);
    put("FTYPE", filterType);
    put("RES", resonance);
    return j;
}

struct MidiEvent { int block; int type; int a; int b; }; // type 0 on, 1 off, 2 bend, 3 cc

static std::vector<MidiEvent> performance()
{
    return {
        { 0, 0, 48, 30 }, { 3, 0, 64, 127 }, { 9, 3, 1, 64 }, { 12, 2, 12000, 0 }, { 15, 0, 55, 90 },
        { 18, 3, 64, 127 }, { 20, 1, 48, 0 }, { 24, 2, 2000, 0 }, { 26, 1, 64, 0 }, { 28, 0, 72, 64 },
        { 30, 3, 1, 0 }, { 33, 3, 64, 0 }, { 34, 2, 8192, 0 }, { 36, 0, 48, 127 }, { 40, 1, 55, 0 },
        { 44, 1, 72, 0 }, { 45, 0, 60, 1 }, { 47, 0, 61, 100 }, { 50, 1, 48, 0 }, { 52, 1, 60, 0 },
        { 53, 1, 61, 0 },
    };
}

static void testOldPatchRendersBitIdentical()
{
    std::cout << "testOldPatchRendersBitIdentical\n";
    struct Scenario { const char* name; std::string json; };
    const Scenario scenarios[] = {
        { "LP + amp-env mod + mod wheel", oldPatchJson("0", "1800", "0.6", "0.7", "linear") },
        { "HP, hard curve", oldPatchJson("1", "350", "0.35", "-0.4", "hard") },
        { "BP, soft curve, no env", oldPatchJson("2", "1200", "0.8", "0", "soft") },
        { "default sound", oldPatchJson("0", "12000", "0.2", "0", "linear") },
    };
    for (const auto& sc : scenarios)
    {
        const auto patch = PatchStore::fromJson(sc.json);
        CHECK(patch.has_value());
        if (!patch)
            continue;
        // An old patch has none of the added parameters
        for (const auto& added : sound::kAddedParams)
            CHECK(patch->params.count(added.id) == 0);

        SamplePool pool;
        pool.setBuffer("lo", richBuffer(130.81, 2.0));
        pool.setBuffer("hi", richBuffer(392.0, 2.0));
        auto map = std::make_shared<InstrumentMap>(patch->map);

        // v1: engine + router driven exactly like the v1 processor (syncParamsToEngine per block)
        looper_v1::VoiceEngine oldEngine;
        looper_v1::MidiRouter oldRouter;
        oldRouter.setEngine(&oldEngine);
        oldEngine.setSamplePool(&pool);
        oldEngine.adoptMap(map);
        oldEngine.setPolyphony(map->polyphonyLimit);
        oldEngine.setSampleRate(48000.0);
        oldRouter.setBendRangeSemis(2.0f);
        auto pv = [&patch](const char* id) { return static_cast<float>(patch->params.at(id)); };
        auto syncOld = [&] {
            looper_v1::AmpEnv::Params env { pv("attack"), pv("decay"), pv("sustain"), pv("release") };
            oldEngine.setEnvParams(env);
            oldEngine.setMasterGainLin(sound::decibelsToGain(pv("volume")));
            looper_v1::FilterParams fp;
            const int t = static_cast<int>(pv("filterType"));
            fp.type = t == 1 ? looper_v1::FilterType::HighPass : t == 2 ? looper_v1::FilterType::BandPass
                                                                         : looper_v1::FilterType::LowPass;
            fp.cutoffHz = pv("cutoff");
            fp.resonance = pv("resonance");
            fp.envAmount = pv("filterEnvAmt");
            oldEngine.setFilterParams(fp);
        };

        // Now: the processor path (patch params, missing added params -> v1 values, SoundParams)
        VoiceEngine newEngine;
        MidiRouter newRouter;
        newRouter.setEngine(&newEngine);
        newEngine.setSamplePool(&pool);
        newEngine.adoptMap(map);
        newEngine.setPolyphony(map->polyphonyLimit);
        newEngine.setSampleRate(48000.0);
        const auto sp = sound::fromRawValues([&patch](const char* id) {
            const auto it = patch->params.find(id);
            return it != patch->params.end() ? static_cast<float>(it->second) : sound::addedParamDefault(id);
        });
        CHECK(sp.velocity.toAmp == 1.0f && sp.velocity.toAttack == 0.0f && sp.filter.keyTrack == 0.0f);
        CHECK(sp.filter.fenvAmountSemis == 0.0f && sp.filter.velToCutoffSemis == 0.0f);
        CHECK(sp.bendUpSemis == 2.0f && sp.bendDownSemis == 2.0f);

        const auto events = performance();
        const int blockSizes[] = { 480, 512, 37, 1024, 256, 1 , 300 };
        std::vector<float> l1(1024), r1(1024), l2(1024), r2(1024);
        bool identical = true;
        double energy = 0.0;
        size_t frames = 0;
        for (int block = 0; block < 70; ++block)
        {
            const int n = blockSizes[block % 7];
            syncOld();
            sound::applyToEngine(sp, newEngine, newRouter);
            for (const auto& e : events)
            {
                if (e.block != block)
                    continue;
                switch (e.type)
                {
                    case 0: oldRouter.handleNoteOn(e.a, e.b, 1); newRouter.handleNoteOn(e.a, e.b, 1); break;
                    case 1: oldRouter.handleNoteOff(e.a, 1); newRouter.handleNoteOff(e.a, 1); break;
                    case 2: oldRouter.handlePitchBend(e.a, 1); newRouter.handlePitchBend(e.a, 1); break;
                    default: oldRouter.handleCc(e.a, e.b, 1); newRouter.handleCc(e.a, e.b, 1); break;
                }
            }
            oldEngine.processBlock(l1.data(), r1.data(), n);
            newEngine.processBlock(l2.data(), r2.data(), n);
            identical = identical && std::memcmp(l1.data(), l2.data(), sizeof(float) * static_cast<size_t>(n)) == 0
                        && std::memcmp(r1.data(), r2.data(), sizeof(float) * static_cast<size_t>(n)) == 0;
            for (int i = 0; i < n; ++i)
                energy += static_cast<double>(l2[static_cast<size_t>(i)]) * l2[static_cast<size_t>(i)];
            frames += static_cast<size_t>(n);
        }
        std::cout << "  " << sc.name << ": " << frames << " frames, rms "
                  << std::sqrt(energy / static_cast<double>(frames)) << (identical ? "  bit-identical\n" : "  DIFFERS\n");
        CHECK(identical);
        CHECK(energy > 1.0); // the render actually made sound
    }
}

static void testPatchParamsRoundTrip()
{
    std::cout << "testPatchParamsRoundTrip\n";
    CHECK(sound::kPatchParamIds.size() == 9 + sound::kAddedParams.size());
    for (const auto& added : sound::kAddedParams)
    {
        CHECK(sound::isAddedParam(added.id));
        bool listed = false;
        for (auto* id : sound::kPatchParamIds)
            listed = listed || std::string(id) == added.id;
        CHECK(listed);
    }
    CHECK(!sound::isAddedParam("cutoff"));

    Patch p;
    p.name = "Shaped";
    p.params["filterType"] = 3;
    p.params["fenvAmount"] = -17.25;
    p.params["fenvAttack"] = 12.5;
    p.params["keyTrack"] = 66.6;
    p.params["velAmp"] = 40.0;
    p.params["velCutoff"] = 9.0;
    p.params["velAttack"] = 75.0;
    p.params["bendUp"] = 12;
    p.params["bendDown"] = 24;
    const auto back = PatchStore::fromJson(PatchStore::toJson(p));
    CHECK(back.has_value());
    if (!back)
        return;
    for (const auto& kv : p.params)
        CHECK(back->params.count(kv.first) == 1 && back->params.at(kv.first) == kv.second);
    const auto sp = sound::fromRawValues([&back](const char* id) {
        const auto it = back->params.find(id);
        return it != back->params.end() ? static_cast<float>(it->second) : sound::addedParamDefault(id, 0.0f);
    });
    CHECK(sp.filter.type == FilterType::LowPass24);
    CHECK_NEAR(sp.filter.fenvAmountSemis, -17.25, 1e-6);
    CHECK_NEAR(sp.filter.keyTrack, 0.666, 1e-6);
    CHECK_NEAR(sp.velocity.toAmp, 0.4, 1e-6);
    CHECK_NEAR(sp.velocity.toAttack, 0.75, 1e-6);
    CHECK(sp.bendUpSemis == 12.0f && sp.bendDownSemis == 24.0f);
    CHECK(sound::filterTypeFromIndex(0) == FilterType::LowPass && sound::filterTypeFromIndex(1) == FilterType::HighPass
          && sound::filterTypeFromIndex(2) == FilterType::BandPass && sound::filterTypeFromIndex(9) == FilterType::LowPass);
}

int main()
{
    testFilterResponsesPerType();
    testTwelveDbTypesBitIdenticalToV1();
    testFilterEnvModulatesCutoff();
    testKeyTrackingMath();
    testVelocityCurves();
    testPitchBendUpDown();
    testParamSmoothingWithoutDiscontinuities();
    testStabilityAtExtremeSettings();
    testRealtimeNoAllocation();
    testOldPatchRendersBitIdentical();
    testPatchParamsRoundTrip();

    std::cout << "\nSoundShapingTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
