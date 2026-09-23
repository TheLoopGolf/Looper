#include "../Source/VoiceEngine/Hermite.h"
#include "../Source/AmpEnv/AmpEnv.h"
#include "../Source/Filter/SvfFilter.h"
#include "../Source/SamplePool/SamplePool.h"
#include "../Source/VoiceEngine/VoiceEngine.h"

#include <cmath>
#include <string>
#include <iostream>
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

#define CHECK_NEAR(a, b, eps) do { \
    const double _a = (double)(a); const double _b = (double)(b); \
    if (std::fabs(_a - _b) > (eps)) { \
        std::cerr << "FAIL: " << #a << " ~= " << #b << " (" << _a << " vs " << _b \
                  << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static void testHermiteKnownVector()
{
    std::cout << "testHermiteKnownVector\n";

    CHECK_NEAR(hermite4(0.0f, 1.0f, 2.0f, 3.0f, 0.0f), 1.0f, 1e-6);

    const float data[] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    CHECK_NEAR(hermiteMono(data, 5, 2.0), 2.0f, 1e-5);
    CHECK_NEAR(hermiteMono(data, 5, 0.0), 0.0f, 1e-5);
    CHECK_NEAR(hermiteMono(data, 5, 1.5), 1.5f, 1e-4);
    CHECK_NEAR(hermiteMono(data, 5, -1.0), 0.0f, 1e-6);

    const float stereo[] = { 1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f, 4.0f, 40.0f };
    float l = 0, r = 0;
    hermiteStereo(stereo, 4, 1.0, l, r);
    CHECK_NEAR(l, 2.0f, 1e-5);
    CHECK_NEAR(r, 20.0f, 1e-5);

    hermiteRead(data, 1, 5, 2.0, l, r);
    CHECK_NEAR(l, 2.0f, 1e-5);
    CHECK_NEAR(r, 2.0f, 1e-5);
}

static void testAmpEnvSustainThenReleaseIdle()
{
    std::cout << "testAmpEnvSustainThenReleaseIdle\n";

    AmpEnv env;
    env.setSampleRate(1000.0);
    AmpEnv::Params p;
    p.attackMs = 10.0f;
    p.decayMs = 10.0f;
    p.sustain = 0.5f;
    p.releaseMs = 20.0f;
    env.setParams(p);

    CHECK(env.isIdle());
    env.noteOn();
    CHECK(env.stage() == AmpEnv::Stage::Attack);

    for (int i = 0; i < 30; ++i)
        env.process();
    CHECK(env.stage() == AmpEnv::Stage::Sustain);
    CHECK_NEAR(env.level(), 0.5f, 0.05);

    env.noteOff();
    CHECK(env.stage() == AmpEnv::Stage::Release);

    float last = 1.0f;
    bool reachedIdle = false;
    for (int i = 0; i < 100; ++i)
    {
        float g = env.process();
        CHECK(g <= last + 1e-4f);
        last = g;
        if (env.isIdle())
        {
            reachedIdle = true;
            CHECK_NEAR(g, 0.0f, 1e-6);
            break;
        }
    }
    CHECK(reachedIdle);
    CHECK(env.process() == 0.0f);
}

static void testAmpEnvMinAttackAndFromCurrent()
{
    std::cout << "testAmpEnvMinAttackAndFromCurrent\n";
    AmpEnv env;
    env.setSampleRate(48000.0);
    AmpEnv::Params p;
    p.attackMs = 0.0f; // clamp to 0.1 ms
    p.decayMs = 50.0f;
    p.sustain = 0.6f;
    p.releaseMs = 50.0f;
    env.setParams(p);
    env.noteOn();
    float g0 = env.process();
    CHECK(g0 > 0.0f);
    CHECK(g0 < 1.0f);

    // Reach sustain at 0.6, then retrigger from current → Attack
    for (int i = 0; i < 20000; ++i)
        env.process();
    CHECK(env.stage() == AmpEnv::Stage::Sustain);
    const float before = env.level();
    CHECK_NEAR(before, 0.6f, 0.05);
    env.noteOn(true);
    CHECK(env.stage() == AmpEnv::Stage::Attack);
    CHECK_NEAR(env.level(), before, 1e-5);
    float g1 = env.process();
    CHECK(g1 > before); // climbing toward 1
}

static void testSvfLowPassAttenuatesHf()
{
    std::cout << "testSvfLowPassAttenuatesHf\n";

    const double sr = 44100.0;
    SvfFilter lp;
    lp.setSampleRate(sr);
    lp.setType(FilterType::LowPass);
    lp.setCutoffHz(200.0f);
    lp.setResonance(0.1f);
    lp.reset();

    // High-frequency tone (~8 kHz) should be attenuated vs DC / bypass energy
    const float freq = 8000.0f;
    const float w = static_cast<float>(2.0 * 3.14159265358979323846 * freq / sr);
    double energyIn = 0.0;
    double energyOut = 0.0;
    // Warm-up then measure
    for (int i = 0; i < 2048; ++i)
    {
        const float x = std::sin(w * static_cast<float>(i));
        const float y = lp.process(x);
        if (i >= 512)
        {
            energyIn += static_cast<double>(x * x);
            energyOut += static_cast<double>(y * y);
        }
    }
    CHECK(energyIn > 1.0);
    CHECK(energyOut < energyIn * 0.15); // strongly attenuated at 8 kHz with 200 Hz LP

    // Impulse → reset clears state (second impulse response starts from zero)
    SvfFilter f2;
    f2.setSampleRate(sr);
    f2.setType(FilterType::LowPass);
    f2.setCutoffHz(1000.0f);
    f2.setResonance(0.3f);
    f2.reset();
    (void) f2.process(1.0f);
    float ring1 = f2.process(0.0f);
    CHECK(std::fabs(ring1) > 1e-6f);
    f2.reset();
    float afterReset = f2.process(0.0f);
    CHECK_NEAR(afterReset, 0.0f, 1e-7);

    // DC through LP should pass (~unity after settle)
    SvfFilter dcLp;
    dcLp.setSampleRate(sr);
    dcLp.setType(FilterType::LowPass);
    dcLp.setCutoffHz(1000.0f);
    dcLp.setResonance(0.2f);
    dcLp.reset();
    float y = 0.0f;
    for (int i = 0; i < 4096; ++i)
        y = dcLp.process(1.0f);
    CHECK_NEAR(y, 1.0f, 0.05);

    CHECK(SvfFilter::resonanceToQ(0.0f) >= 0.45f);
    CHECK(SvfFilter::resonanceToQ(1.0f) <= 10.0f);
    CHECK(SvfFilter::resonanceToQ(1.0f) > SvfFilter::resonanceToQ(0.0f));
}

static void testVoiceEngineOfflineDemoEnergy()
{
    std::cout << "testVoiceEngineOfflineDemoEnergy\n";

    SamplePool pool;
    pool.loadDemoSample(44100.0);

    InstrumentMap map;
    map.zones.push_back(makeDemoZone());
    map.polyphonyLimit = 8;

    VoiceEngine engine;
    engine.setSamplePool(&pool);
    engine.setMap(&map);
    engine.setSampleRate(44100.0);
    engine.setPolyphony(8);

    AmpEnv::Params env;
    env.attackMs = 1.0f;
    env.decayMs = 50.0f;
    env.sustain = 0.7f;
    env.releaseMs = 200.0f;
    engine.setEnvParams(env);
    engine.setMasterGainLin(1.0f);

    constexpr int kBlock = 512;
    std::vector<float> left(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> right(static_cast<size_t>(kBlock), 0.0f);

    engine.noteOn(60, 100, 1);
    CHECK(engine.activeVoiceCount() == 1);

    double energy = 0.0;
    // ~100 ms — well within demo length and sustain
    for (int b = 0; b < 10; ++b)
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        engine.processBlock(left.data(), right.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            energy += static_cast<double>(left[i] * left[i] + right[i] * right[i]);
    }

    CHECK(engine.activeVoiceCount() >= 1);
    CHECK(energy > 1e-4);

    engine.noteOff(60, 1);
    for (int b = 0; b < 40; ++b)
        engine.processBlock(left.data(), right.data(), kBlock);

    engine.noteOn(72, 100, 1);
    double energyHigh = 0.0;
    for (int b = 0; b < 10; ++b)
    {
        engine.processBlock(left.data(), right.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            energyHigh += static_cast<double>(left[i] * left[i] + right[i] * right[i]);
    }
    CHECK(energyHigh > 1e-4);

    VoiceEngine stealEngine;
    stealEngine.setSamplePool(&pool);
    stealEngine.setMap(&map);
    stealEngine.setSampleRate(44100.0);
    stealEngine.setPolyphony(2);
    stealEngine.setEnvParams(env);
    stealEngine.noteOn(60, 100, 1);
    stealEngine.noteOn(62, 100, 1);
    CHECK(stealEngine.activeVoiceCount() == 2);
    stealEngine.noteOn(64, 100, 1);
    CHECK(stealEngine.activeVoiceCount() == 2);
}

static void testVoiceEngineFilterChangesOutput()
{
    std::cout << "testVoiceEngineFilterChangesOutput\n";

    SamplePool pool;
    pool.loadDemoSample(44100.0);

    InstrumentMap map;
    map.zones.push_back(makeDemoZone());
    map.polyphonyLimit = 4;

    AmpEnv::Params env;
    env.attackMs = 1.0f;
    env.decayMs = 50.0f;
    env.sustain = 1.0f;
    env.releaseMs = 100.0f;

    auto renderEnergy = [&](const FilterParams& fp) -> double {
        VoiceEngine engine;
        engine.setSamplePool(&pool);
        engine.setMap(&map);
        engine.setSampleRate(44100.0);
        engine.setPolyphony(4);
        engine.setEnvParams(env);
        engine.setMasterGainLin(1.0f);
        engine.setFilterParams(fp);

        constexpr int kBlock = 256;
        std::vector<float> left(static_cast<size_t>(kBlock), 0.0f);
        std::vector<float> right(static_cast<size_t>(kBlock), 0.0f);
        engine.noteOn(60, 127, 1);
        double e = 0.0;
        for (int b = 0; b < 20; ++b)
        {
            engine.processBlock(left.data(), right.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                e += static_cast<double>(left[i] * left[i] + right[i] * right[i]);
        }
        CHECK(engine.activeVoiceCount() >= 1);
        return e;
    };

    FilterParams openLp;
    openLp.type = FilterType::LowPass;
    openLp.cutoffHz = 18000.0f;
    openLp.resonance = 0.1f;
    openLp.envAmount = 0.0f;

    FilterParams closedLp;
    closedLp.type = FilterType::LowPass;
    closedLp.cutoffHz = 80.0f;
    closedLp.resonance = 0.2f;
    closedLp.envAmount = 0.0f;

    FilterParams envSweep;
    envSweep.type = FilterType::LowPass;
    envSweep.cutoffHz = 200.0f;
    envSweep.resonance = 0.3f;
    envSweep.envAmount = 1.0f; // +octaves as amp env rises

    const double eOpen = renderEnergy(openLp);
    const double eClosed = renderEnergy(closedLp);
    const double eEnv = renderEnergy(envSweep);

    CHECK(eOpen > 1e-4);
    CHECK(eClosed > 1e-6); // still non-silent
    CHECK(eClosed < eOpen * 0.85); // low cutoff removes HF energy from demo tone
    CHECK(eEnv > 1e-4);
    CHECK(std::fabs(eEnv - eClosed) > 1e-6); // env amount changes output vs fixed low cutoff
}

static void testSamplePoolSetGet()
{
    std::cout << "testSamplePoolSetGet\n";
    SamplePool pool;
    CHECK(!pool.contains("x"));
    SampleBuffer b;
    b.channels = 1;
    b.sampleRate = 48000.0;
    b.interleaved = { 0.1f, 0.2f, 0.3f };
    b.length = 3;
    pool.setBuffer("x", std::move(b));
    CHECK(pool.contains("x"));
    auto got = pool.getBuffer("x");
    CHECK(got != nullptr);
    CHECK(got->length == 3);
    CHECK_NEAR(got->interleaved[1], 0.2f, 1e-6);

    pool.loadDemoSample(44100.0);
    CHECK(pool.contains(kDemoSampleId));
    auto demo = pool.getBuffer(kDemoSampleId);
    CHECK(demo != nullptr);
    CHECK(demo->length > 1000);
}


static Zone makeZone(const std::string& sampleId, int root, int keyLo, int keyHi,
                     int velLo, int velHi, int rrGroup = 0, int rrIndex = 0)
{
    Zone z;
    z.sampleId = sampleId;
    z.rootKey = root;
    z.keyLow = keyLo;
    z.keyHigh = keyHi;
    z.velLow = velLo;
    z.velHigh = velHi;
    z.rrGroup = rrGroup;
    z.rrIndex = rrIndex;
    return z;
}

static void testVelocityLayersSelectZone()
{
    std::cout << "testVelocityLayersSelectZone\n";

    InstrumentMap map;
    map.zones.push_back(makeZone("soft", 60, 0, 127, 1, 64));
    map.zones.push_back(makeZone("hard", 60, 0, 127, 65, 127));

    VoiceEngine engine;
    engine.setMap(&map);

    const Zone* soft = engine.selectZone(60, 40);
    CHECK(soft != nullptr);
    CHECK(soft->sampleId == "soft");

    const Zone* hard = engine.selectZone(60, 100);
    CHECK(hard != nullptr);
    CHECK(hard->sampleId == "hard");

    // Boundary: vel 64 → soft, 65 → hard
    CHECK(engine.selectZone(60, 64)->sampleId == "soft");
    CHECK(engine.selectZone(60, 65)->sampleId == "hard");
}

static void testRoundRobinCycle()
{
    std::cout << "testRoundRobinCycle\n";

    InstrumentMap map;
    // Four RR zones, same key/vel, rrGroup=1, rrIndex 0..3 (out of order in map to test sort)
    map.zones.push_back(makeZone("rr2", 60, 48, 72, 1, 127, 1, 2));
    map.zones.push_back(makeZone("rr0", 60, 48, 72, 1, 127, 1, 0));
    map.zones.push_back(makeZone("rr3", 60, 48, 72, 1, 127, 1, 3));
    map.zones.push_back(makeZone("rr1", 60, 48, 72, 1, 127, 1, 1));

    VoiceEngine engine;
    engine.setMap(&map);

    const char* expected[] = { "rr0", "rr1", "rr2", "rr3", "rr0", "rr1" };
    for (const char* id : expected)
    {
        const Zone* z = engine.selectZone(60, 100);
        CHECK(z != nullptr);
        CHECK(z->sampleId == id);
    }

    // setMap clears counters → restart at rr0
    engine.setMap(&map);
    CHECK(engine.selectZone(60, 80)->sampleId == "rr0");

    // mapChanged also clears
    (void) engine.selectZone(60, 80); // rr1
    engine.mapChanged();
    CHECK(engine.selectZone(60, 80)->sampleId == "rr0");
}

static void testRrGroupZeroFirstWins()
{
    std::cout << "testRrGroupZeroFirstWins\n";

    InstrumentMap map;
    map.zones.push_back(makeZone("first", 60, 0, 127, 1, 127, 0, 0));
    map.zones.push_back(makeZone("second", 60, 0, 127, 1, 127, 0, 1));
    map.zones.push_back(makeZone("third", 60, 0, 127, 1, 127, 0, 2));

    VoiceEngine engine;
    engine.setMap(&map);

    for (int i = 0; i < 5; ++i)
    {
        const Zone* z = engine.selectZone(60, 64);
        CHECK(z != nullptr);
        CHECK(z->sampleId == "first");
    }
}

static void testSelectZoneNoMatch()
{
    std::cout << "testSelectZoneNoMatch\n";

    InstrumentMap map;
    map.zones.push_back(makeZone("only", 60, 60, 60, 1, 127));

    VoiceEngine engine;
    engine.setMap(&map);

    CHECK(engine.selectZone(61, 100) == nullptr);
    CHECK(engine.selectZone(60, 100) != nullptr);
    CHECK(engine.selectZone(60, 100)->sampleId == "only");

    VoiceEngine emptyEngine;
    CHECK(emptyEngine.selectZone(60, 100) == nullptr);
}

int main()
{
    testHermiteKnownVector();
    testAmpEnvSustainThenReleaseIdle();
    testAmpEnvMinAttackAndFromCurrent();
    testSamplePoolSetGet();
    testSvfLowPassAttenuatesHf();
    testVoiceEngineOfflineDemoEnergy();
    testVoiceEngineFilterChangesOutput();
    testVelocityLayersSelectZone();
    testRoundRobinCycle();
    testRrGroupZeroFirstWins();
    testSelectZoneNoMatch();

    std::cout << "\nPassed: " << g_passed << "  Failed: " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
