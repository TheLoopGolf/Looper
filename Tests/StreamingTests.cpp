// Disk streaming: preload + background-streamed remainder must play bit-identically to a fully
// loaded sample (all pitch ratios, across the preload/stream boundary, with glides, from a
// sample-start offset), underruns must fade instead of glitching and be counted, voice steals
// and note ends must free stream slots, offline rendering must never underrun, the audio thread
// must not allocate, and many concurrent voices must work. With JUCE (LOOPER_STREAMING_WITH_JUCE)
// the real file path is covered too: ImportController preload decode + FileStreamSource.

#include "../Source/PatchStore/PatchStore.h"
#include "../Source/Prefs/SessionPrefs.h"
#include "../Source/SamplePool/MemoryFormat.h"
#include "../Source/SamplePool/SamplePool.h"
#include "../Source/VoiceEngine/DiskStreamer.h"
#include "../Source/VoiceEngine/VoiceEngine.h"

#if LOOPER_STREAMING_WITH_JUCE
 #include "../Source/Import/FileStreamSource.h"
 #include "../Source/Import/ImportController.h"
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace looper;

#if defined(__GNUC__) && !defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// --- Allocation counter: only counts on the thread that armed it (the "audio thread") --------
static thread_local bool t_countAllocs = false;
static std::atomic<long> g_audioAllocs { 0 };
static std::atomic<long> g_audioFrees { 0 };
void* operator new(std::size_t n)
{
    if (t_countAllocs) ++g_audioAllocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    if (t_countAllocs) ++g_audioAllocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p && t_countAllocs) ++g_audioFrees; std::free(p); }
void operator delete[](void* p) noexcept { if (p && t_countAllocs) ++g_audioFrees; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p && t_countAllocs) ++g_audioFrees; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p && t_countAllocs) ++g_audioFrees; std::free(p); }

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

// --- Signals / fixtures ---------------------------------------------------------------------------

/** Rich, non-periodic test audio (partials + seeded noise) so interpolation differences show. */
static SampleBuffer makeSignal(int channels, double sr, double seconds, uint32_t seed = 1)
{
    SampleBuffer b;
    b.channels = channels;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(sr * seconds);
    b.interleaved.resize(static_cast<size_t>(b.length * channels));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (int64_t i = 0; i < b.length; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        for (int c = 0; c < channels; ++c)
        {
            const double f = 220.0 * (1.0 + 0.37 * c) + 13.0 * seed;
            const double s = 0.30 * std::sin(6.283185307179586 * f * t)
                           + 0.15 * std::sin(6.283185307179586 * f * 2.71 * t + 0.4)
                           + 0.08 * std::sin(6.283185307179586 * 3111.0 * t);
            b.interleaved[static_cast<size_t>(i * channels + c)] = static_cast<float>(s) + 0.05f * noise(rng);
        }
    }
    b.residentFrames = b.length;
    return b;
}

static Zone fullZone(const std::string& id, int root = 60)
{
    Zone z;
    z.sampleId = id;
    z.rootKey = root;
    z.keyLow = 0;
    z.keyHigh = 127;
    return z;
}

static AmpEnv::Params sustainEnv()
{
    AmpEnv::Params p;
    p.attackMs = 1.0f;
    p.decayMs = 50.0f;
    p.sustain = 1.0f;
    p.releaseMs = 30.0f;
    return p;
}

/** Pool + streamer + engine wired like the plugin (declaration order = destruction safety). */
struct Rig
{
    SamplePool pool;
    DiskStreamer streamer;
    VoiceEngine engine;
    InstrumentMap map;

    explicit Rig(StreamerConfig cfg = threadless(), int polyphony = 64) : streamer(cfg)
    {
        engine.setSamplePool(&pool);
        engine.setStreamer(&streamer);
        engine.setSampleRate(44100.0);
        engine.setPolyphony(polyphony);
        engine.setEnvParams(sustainEnv());
        streamer.ensureRings();
    }

    static StreamerConfig threadless()
    {
        StreamerConfig c;
        c.numThreads = 0;
        return c;
    }

    void useMap() { engine.setMap(&map); }
};

using Hook = std::function<void(int block)>;

/** Render `blocks` x `blockSize` stereo, calling `before(block)` ahead of each block. */
static std::vector<float> render(VoiceEngine& e, int blocks, int blockSize, const Hook& before = {})
{
    std::vector<float> out(static_cast<size_t>(blocks * blockSize * 2));
    std::vector<float> l(static_cast<size_t>(blockSize)), r(static_cast<size_t>(blockSize));
    for (int b = 0; b < blocks; ++b)
    {
        if (before) before(b);
        e.processBlock(l.data(), r.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            out[static_cast<size_t>((b * blockSize + i) * 2)] = l[static_cast<size_t>(i)];
            out[static_cast<size_t>((b * blockSize + i) * 2 + 1)] = r[static_cast<size_t>(i)];
        }
    }
    return out;
}

static bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b, size_t* firstDiff = nullptr)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
        {
            if (firstDiff) *firstDiff = i;
            return false;
        }
    return true;
}

static bool allFinite(const std::vector<float>& v)
{
    for (float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}

static double peakAbs(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    double p = 0.0;
    for (size_t i = from; i < std::min(to, v.size()); ++i)
        p = std::max(p, static_cast<double>(std::fabs(v[i])));
    return p;
}

// --- Tests ----------------------------------------------------------------------------------------

static void testBufferBasics()
{
    std::cout << "testBufferBasics\n";
    const auto full = makeSignal(2, 44100.0, 1.0);
    const auto s = makeStreamedCopy(full, 4096);
    CHECK(s.isStreaming());
    CHECK_EQ(s.residentLength(), static_cast<int64_t>(4096));
    CHECK_EQ(s.length, full.length);
    CHECK(s.residentBytes() < full.residentBytes() / 8);
    CHECK_EQ(s.fullBytes(), full.fullBytes());

    // copyFrames spans RAM + stream and matches the full decode exactly
    std::vector<float> got(static_cast<size_t>(full.length * 2));
    CHECK_EQ(s.copyFrames(0, full.length, got.data()), full.length);
    CHECK(bitIdentical(got, full.interleaved));

    // Small samples (shorter than the preload) stay fully in RAM
    const auto shortBuf = makeSignal(1, 44100.0, 0.1);
    const auto small = makeStreamedCopy(shortBuf, 65536);
    CHECK(!small.isStreaming());
    CHECK_EQ(small.residentLength(), shortBuf.length);

    SamplePool pool;
    pool.setBuffer("s", s);
    pool.setBuffer("small", small);
    const auto st = pool.memoryStats();
    CHECK_EQ(st.samples, 2);
    CHECK_EQ(st.streamingSamples, 1);
    CHECK(st.residentBytes < st.fullBytes);

    // A stream that covers nothing beyond the resident part is dropped by normalisation
    SampleBuffer odd = small;
    odd.stream = std::make_shared<MemoryStreamSource>(std::make_shared<const std::vector<float>>(small.interleaved), 1);
    normaliseSampleBuffer(odd);
    CHECK(!odd.isStreaming());
    CHECK(odd.stream == nullptr);
}

static void testMemoryFormat()
{
    std::cout << "testMemoryFormat\n";
    CHECK_EQ(formatBytes(512 * 1024), std::string("512 KB"));
    CHECK_EQ(formatBytes(static_cast<size_t>(4.2 * 1024 * 1024)), std::string("4.2 MB"));
    CHECK_EQ(formatBytes(static_cast<size_t>(42) * 1024 * 1024), std::string("42 MB"));
    CHECK_EQ(formatBytes(static_cast<size_t>(1536) * 1024 * 1024), std::string("1.5 GB"));
    auto a = describeMemory(static_cast<size_t>(42) * 1024 * 1024, 3, 0);
    CHECK_EQ(a.ram, std::string("RAM 42 MB"));
    CHECK_EQ(a.state, std::string("Streaming"));
    CHECK(a.tone == MemoryTone::Streaming);
    auto b = describeMemory(1024 * 1024 * 12, 0, 0);
    CHECK_EQ(b.state, std::string("In RAM"));
    CHECK(b.tone == MemoryTone::InRam);
    auto c = describeMemory(1024 * 1024 * 12, 5, 3);
    CHECK_EQ(c.state, std::string("3 dropouts"));
    CHECK(c.tone == MemoryTone::Warning);
    CHECK_EQ(describeMemory(1, 5, 1).state, std::string("1 dropout"));
    CHECK_EQ(formatPreloadFrames(65536), std::string("64k frames"));
}

/**
 * Same notes on a fully loaded engine and a streamed one (tiny odd-sized preload so every voice
 * crosses the boundary at fractional positions). Output must match bit for bit.
 */
static void compareFullVsStreamed(const char* label, int channels, double fileRate, int64_t preload,
                                  const std::vector<std::pair<int, float>>& notesAndCents,
                                  float glideMs = 0.0f, std::optional<int64_t> sampleStart = {},
                                  bool offline = false, int blockSize = 512, int blocks = 200)
{
    const auto full = makeSignal(channels, fileRate, 3.0, 7);

    Rig a, b;
    a.pool.setBuffer("x", full);
    b.pool.setBuffer("x", makeStreamedCopy(full, preload));
    for (Rig* r : { &a, &b })
    {
        Zone z = fullZone("x");
        z.sampleStart = sampleStart;
        r->map.zones = { z };
        r->map.glideMs = glideMs;
        r->useMap();
        r->engine.setNonRealtime(offline);
    }

    auto play = [&](Rig& r, bool streamed) {
        return render(r.engine, blocks, blockSize, [&](int block) {
            // Notes start at different blocks so voices overlap at different positions
            for (size_t k = 0; k < notesAndCents.size(); ++k)
                if (block == static_cast<int>(k) * 7)
                {
                    r.map.zones[0].tuneCents = notesAndCents[k].second;
                    r.engine.noteOn(notesAndCents[k].first, 100, 1);
                }
            if (streamed && !offline)
                r.streamer.serviceUntilIdle(); // deterministic "disk" between blocks
        });
    };
    const auto outFull = play(a, false);
    const auto outStream = play(b, true);
    size_t diff = 0;
    const bool same = bitIdentical(outFull, outStream, &diff);
    if (!same)
        std::cerr << "  " << label << ": first difference at float " << diff << " (" << outFull[diff]
                  << " vs " << outStream[diff] << ")\n";
    CHECK(same);
    CHECK(peakAbs(outStream) > 0.01);               // it actually played
    CHECK_EQ(b.engine.underrunCount(), static_cast<uint64_t>(0));
    CHECK(b.streamer.framesStreamed() > 0);         // and actually streamed
}

static void testBitExactPitchRatios()
{
    std::cout << "testBitExactPitchRatios\n";
    // Ratios: 1, 0.5 (-1 oct), 1.4 (+5 st +45 ct), 2 (+1 oct), 4 (+2 oct), 0.37
    const std::vector<std::pair<int, float>> notes = { { 60, 0.0f }, { 48, 0.0f }, { 65, 45.0f },
                                                       { 72, 0.0f }, { 84, 0.0f }, { 43, -21.0f } };
    compareFullVsStreamed("mono 44.1k", 1, 44100.0, 3001, notes);
    compareFullVsStreamed("stereo 44.1k", 2, 44100.0, 3001, notes);
    // File rate != host rate (48k file on a 44.1k engine) adds a 1.088 factor on top
    compareFullVsStreamed("stereo 48k", 2, 48000.0, 4097, notes);
    // +2 octaves of a 96k file on 44.1k: ~8.7 frames per output sample through the ring
    compareFullVsStreamed("96k +2 oct", 2, 96000.0, 8192, { { 84, 0.0f }, { 83, 30.0f } });
    // Small host blocks / large host blocks (the engine splits long blocks into sub-blocks)
    compareFullVsStreamed("block 64", 2, 44100.0, 2048, notes, 0.0f, {}, false, 64, 1500);
    compareFullVsStreamed("block 4096", 2, 44100.0, 2048, notes, 0.0f, {}, true, 4096, 30);
}

static void testBoundaryCrossing()
{
    std::cout << "testBoundaryCrossing\n";
    // Legato glide sweeping the ratio while the voice crosses the boundary
    compareFullVsStreamed("glide", 2, 44100.0, 5003, { { 60, 0.0f }, { 79, 0.0f }, { 50, 0.0f } }, 120.0f);
    // Sample start beyond the preload: the stream starts at the offset (no preload help)
    compareFullVsStreamed("start past preload", 2, 44100.0, 4096, { { 64, 0.0f } }, 0.0f, 20000, true);
    // Start just before the boundary
    compareFullVsStreamed("start at boundary", 1, 44100.0, 4096, { { 67, 13.0f } }, 0.0f, 4094);

    // Preload exactly 1 frame short of the length / 1 frame: still exact
    compareFullVsStreamed("preload 1 frame", 2, 44100.0, 1, { { 60, 0.0f }, { 72, 0.0f } });

    // Frame-level check right at the seam: fetch through the engine's own path at positions
    // straddling resident end (i1 - 1 .. i1 + 2 spanning both sides).
    const auto full = makeSignal(2, 44100.0, 1.0, 3);
    const auto streamed = makeStreamedCopy(full, 1000);
    for (int64_t start = 990; start < 1010; ++start)
    {
        std::vector<float> got(16), want(16);
        streamed.copyFrames(start, 8, got.data());
        std::copy(full.interleaved.begin() + start * 2, full.interleaved.begin() + (start + 8) * 2, want.begin());
        CHECK(bitIdentical(got, want));
    }
}

static void testUnderrunFadesAndCounts()
{
    std::cout << "testUnderrunFadesAndCounts\n";
    Rig r;   // no reader threads: nothing streams unless we service
    const auto full = makeSignal(1, 44100.0, 2.0, 5);
    const int64_t preload = 2048;
    r.pool.setBuffer("x", makeStreamedCopy(full, preload));
    r.map.zones = { fullZone("x") };
    r.useMap();
    r.engine.noteOn(60, 127, 1);

    // Disk "stalled": play through the preload into an underrun
    const int block = 64;
    auto out = render(r.engine, 64, block); // 4096 samples
    CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(1));
    CHECK(r.engine.activeVoiceCount() == 1);  // voice keeps its place; it is not killed
    // Before the preload end: normal audio. Max per-sample step while playing normally:
    double normalStep = 0.0;
    for (size_t i = 2 * 200; i + 2 < static_cast<size_t>(preload) * 2; i += 2)
        normalStep = std::max(normalStep, static_cast<double>(std::fabs(out[i + 2] - out[i])));
    // Across the dropout: no step larger than normal playback produces (faded, not cut)
    double dropStep = 0.0;
    for (size_t i = static_cast<size_t>(preload - 4) * 2; i + 2 < static_cast<size_t>(preload + 200) * 2; i += 2)
        dropStep = std::max(dropStep, static_cast<double>(std::fabs(out[i + 2] - out[i])));
    CHECK(dropStep <= normalStep * 1.05 + 1e-6);
    // Silence after the fade (filter tail included)
    CHECK(peakAbs(out, static_cast<size_t>(preload + 600) * 2) < 1e-4);
    CHECK(allFinite(out));

    // Disk catches up: the stream skips ahead to the voice and audio fades back in
    r.streamer.serviceUntilIdle();
    auto resumed = render(r.engine, 32, block, [&](int) { r.streamer.serviceUntilIdle(); });
    CHECK(peakAbs(resumed, 2 * 200) > 0.05);
    CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(1));
    // Fade-in, not a jump: first resumed samples ramp up
    CHECK(std::fabs(resumed[0]) < 0.02);

    // A second stall is a second underrun event
    render(r.engine, 400, block);
    CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(2));
    r.engine.resetUnderruns();
    CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(0));

    // Read errors: a failing source never crashes, it just underruns
    Rig e;
    auto broken = makeStreamedCopy(full, 1024);
    auto* src = static_cast<MemoryStreamSource*>(broken.stream.get());
    src->setFailing(true);
    e.pool.setBuffer("x", broken);
    e.map.zones = { fullZone("x") };
    e.useMap();
    e.engine.noteOn(60, 100, 1);
    auto bad = render(e.engine, 64, 64, [&](int) { e.streamer.serviceUntilIdle(); });
    CHECK(e.streamer.readErrors() >= 1);
    CHECK(e.engine.underrunCount() >= 1);
    CHECK(allFinite(bad));
}

static void testSlowDiskWithThreads()
{
    std::cout << "testSlowDiskWithThreads\n";
    // Real reader threads + a source that takes 30 ms per read, played as fast as the CPU can:
    // underruns are expected, counted, and harmless.
    StreamerConfig cfg;
    cfg.numThreads = 2;
    Rig r(cfg);
    const auto full = makeSignal(2, 44100.0, 3.0, 9);
    auto streamed = makeStreamedCopy(full, 2048);
    static_cast<MemoryStreamSource*>(streamed.stream.get())->setDelayMicros(30000);
    r.pool.setBuffer("x", streamed);
    r.map.zones = { fullZone("x") };
    r.useMap();
    r.streamer.start();
    r.engine.noteOn(72, 100, 1);
    auto out = render(r.engine, 400, 256);
    CHECK(r.engine.underrunCount() >= 1);
    CHECK(allFinite(out));
    r.engine.allNotesOff();
    render(r.engine, 40, 256);
    CHECK_EQ(r.engine.activeVoiceCount(), 0);
    r.streamer.stop(); // joins: the in-flight slow read completes and frees its retiring slot
    CHECK_EQ(r.streamer.activeStreams(), 0);
    CHECK_EQ(r.streamer.freeSlots(), r.streamer.config().numSlots);
}

static void testVoiceStealFreesSlots()
{
    std::cout << "testVoiceStealFreesSlots\n";
    StreamerConfig cfg = Rig::threadless();
    cfg.numSlots = 4;
    Rig r(cfg, 4);   // exactly as many slots as voices
    const auto full = makeSignal(2, 44100.0, 3.0, 2);
    r.pool.setBuffer("x", makeStreamedCopy(full, 4096));
    r.map.zones = { fullZone("x") };
    r.useMap();
    std::vector<float> l(256), rr(256);

    for (int n = 0; n < 4; ++n)
        r.engine.noteOn(60 + n, 100, 1);
    r.engine.processBlock(l.data(), rr.data(), 256);
    CHECK_EQ(r.streamer.activeStreams(), 4);

    // 5th note steals a voice: the stolen voice's slot goes straight to the new note
    r.engine.noteOn(70, 100, 1);
    const Voice* v = r.engine.findActiveVoice(70, 1);
    CHECK(v != nullptr && v->streamSlot >= 0);
    CHECK_EQ(r.streamer.activeStreams(), 4);
    CHECK_EQ(r.streamer.slotsExhausted(), static_cast<uint64_t>(0));

    // Retrigger the same note: same voice, slot recycled
    r.engine.noteOn(70, 90, 1);
    CHECK_EQ(r.streamer.activeStreams(), 4);

    // Hammer: 2000 random note-ons with releases; slots never leak or run out
    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i)
    {
        const int note = 40 + static_cast<int>(rng() % 40);
        r.engine.noteOn(note, 100, 1);
        if (rng() % 3 == 0)
            r.engine.noteOff(note, 1);
        if (i % 5 == 0)
        {
            r.streamer.serviceUntilIdle();
            r.engine.processBlock(l.data(), rr.data(), 256);
        }
        CHECK(r.streamer.activeStreams() <= 4);
    }
    CHECK_EQ(r.streamer.slotsExhausted(), static_cast<uint64_t>(0));

    // Everything released and finished -> every slot free
    r.engine.allNotesOff();
    for (int i = 0; i < 50 && r.engine.activeVoiceCount() > 0; ++i)
        r.engine.processBlock(l.data(), rr.data(), 256);
    CHECK_EQ(r.engine.activeVoiceCount(), 0);
    CHECK_EQ(r.streamer.activeStreams(), 0);

    // Fewer slots than voices: extra voices play their preload, get a slot when one frees up
    StreamerConfig two = Rig::threadless();
    two.numSlots = 2;
    Rig s(two, 4);
    s.pool.setBuffer("x", makeStreamedCopy(full, 4096));
    s.map.zones = { fullZone("x") };
    s.useMap();
    for (int n = 0; n < 3; ++n)
        s.engine.noteOn(60 + n, 100, 1);
    CHECK_EQ(s.streamer.activeStreams(), 2);
    CHECK_EQ(s.streamer.slotsExhausted(), static_cast<uint64_t>(1));
    s.engine.noteOff(60, 1);
    for (int i = 0; i < 20; ++i)
    {
        s.streamer.serviceUntilIdle();
        s.engine.processBlock(l.data(), rr.data(), 256);
    }
    const Voice* late = s.engine.findActiveVoice(62, 1);
    CHECK(late != nullptr && late->streamSlot >= 0); // picked up the freed slot before its preload ran out
    CHECK_EQ(s.engine.underrunCount(), static_cast<uint64_t>(0));

    // Release while a reader is mid-fill (Busy -> BusyRetiring): the reader frees the slot
    StreamerConfig threaded;
    threaded.numSlots = 4;
    threaded.numThreads = 2;
    Rig t(threaded, 4);
    auto slow = makeStreamedCopy(full, 4096);
    static_cast<MemoryStreamSource*>(slow.stream.get())->setDelayMicros(3000);
    t.pool.setBuffer("x", slow);
    t.map.zones = { fullZone("x") };
    t.useMap();
    t.streamer.start();
    for (int i = 0; i < 300; ++i)
    {
        t.engine.noteOn(50 + i % 12, 100, 1);
        t.engine.processBlock(l.data(), rr.data(), 64);
    }
    t.engine.allNotesOff();
    for (int i = 0; i < 100 && t.engine.activeVoiceCount() > 0; ++i)
        t.engine.processBlock(l.data(), rr.data(), 256);
    // Readers finish their in-flight fills and free retiring slots
    for (int i = 0; i < 400 && t.streamer.freeSlots() < 4; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_EQ(t.streamer.activeStreams(), 0);
    CHECK_EQ(t.streamer.freeSlots(), 4);
    for (int n = 0; n < 4; ++n)
        t.engine.noteOn(70 + n, 100, 1);
    CHECK_EQ(t.streamer.activeStreams(), 4);
}

static void testOfflineNeverUnderruns()
{
    std::cout << "testOfflineNeverUnderruns\n";
    // Reader threads exist but are paused: in non-realtime mode the engine reads the disk
    // itself, so a bounce is exact and has no dropouts even with no background help at all.
    const std::vector<std::pair<int, float>> notes = { { 60, 0.0f }, { 84, 0.0f }, { 55, 33.0f } };
    {
        const auto full = makeSignal(2, 48000.0, 3.0, 4);
        Rig a, b(StreamerConfig {});
        a.pool.setBuffer("x", full);
        auto streamed = makeStreamedCopy(full, 1024);
        static_cast<MemoryStreamSource*>(streamed.stream.get())->setDelayMicros(200); // slow disk
        b.pool.setBuffer("x", streamed);
        b.streamer.start();
        b.streamer.setPaused(true);
        for (Rig* r : { &a, &b })
        {
            r->map.zones = { fullZone("x") };
            r->useMap();
        }
        b.engine.setNonRealtime(true);
        auto play = [&](Rig& r) {
            return render(r.engine, 260, 1024, [&](int block) {
                for (size_t k = 0; k < notes.size(); ++k)
                    if (block == static_cast<int>(k) * 5)
                    {
                        r.map.zones[0].tuneCents = notes[k].second;
                        r.engine.noteOn(notes[k].first, 100, 1);
                    }
            });
        };
        const auto fa = play(a);
        const auto fb = play(b);
        CHECK(bitIdentical(fa, fb));
        CHECK_EQ(b.engine.underrunCount(), static_cast<uint64_t>(0));
    }
    // Switching back to realtime with the disk still stalled underruns again (sanity)
    {
        const auto full = makeSignal(1, 44100.0, 2.0, 4);
        Rig r;
        r.pool.setBuffer("x", makeStreamedCopy(full, 1024));
        r.map.zones = { fullZone("x") };
        r.useMap();
        r.engine.setNonRealtime(true);
        r.engine.noteOn(60, 100, 1);
        render(r.engine, 20, 256);
        CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(0));
        r.engine.setNonRealtime(false);
        render(r.engine, 400, 256);
        CHECK(r.engine.underrunCount() >= 1);
    }
}

static void testNoAllocationOnAudioThread()
{
    std::cout << "testNoAllocationOnAudioThread\n";
    StreamerConfig cfg;
    cfg.numThreads = 2;
    Rig r(cfg, 16);
    std::vector<std::string> ids = { "a", "b", "c", "d" };
    for (size_t i = 0; i < ids.size(); ++i)
        r.pool.setBuffer(ids[i], makeStreamedCopy(makeSignal(static_cast<int>(1 + i % 2), 44100.0, 2.0, static_cast<uint32_t>(i)), 2048));
    for (size_t i = 0; i < ids.size(); ++i)
    {
        Zone z = fullZone(ids[i]);
        z.keyLow = static_cast<int>(40 + i * 10);
        z.keyHigh = z.keyLow + 9;
        r.map.zones.push_back(z);
    }
    r.useMap();
    r.streamer.start();
    std::vector<float> l(256), rr(256);
    // Warm-up (first touch of every path)
    r.engine.noteOn(41, 100, 1);
    r.engine.processBlock(l.data(), rr.data(), 256);
    r.engine.noteOff(41, 1);

    g_audioAllocs = 0;
    g_audioFrees = 0;
    t_countAllocs = true;
    for (int b = 0; b < 1500; ++b)
    {
        if (b % 6 == 0) r.engine.noteOn(40 + (b * 7) % 40, 100, 1);       // incl. steals (16 voices)
        if (b % 9 == 0) r.engine.noteOff(40 + (b * 5) % 40, 1);
        if (b % 300 == 0) r.engine.setNonRealtime(b % 600 == 0);          // offline path too
        r.engine.processBlock(l.data(), rr.data(), 256);
    }
    r.engine.allNotesOff();
    for (int b = 0; b < 40; ++b)
        r.engine.processBlock(l.data(), rr.data(), 256);                   // voices end, slots freed
    t_countAllocs = false;
    CHECK_EQ(g_audioAllocs.load(), 0L);
    CHECK_EQ(g_audioFrees.load(), 0L);
}

static void testManyConcurrentVoices()
{
    std::cout << "testManyConcurrentVoices\n";
    // 128 voices over 48 different streamed samples, offline (exact) with reader threads running
    constexpr int kSamples = 48;
    Rig full(Rig::threadless(), 128);
    StreamerConfig cfg;
    cfg.numThreads = 3;
    Rig st(cfg, 128);
    for (int i = 0; i < kSamples; ++i)
    {
        const auto buf = makeSignal(1 + i % 2, 44100.0, 1.5, static_cast<uint32_t>(100 + i));
        const std::string id = "s" + std::to_string(i);
        full.pool.setBuffer(id, buf);
        st.pool.setBuffer(id, makeStreamedCopy(buf, 4096));
        for (Rig* r : { &full, &st })
        {
            Zone z = fullZone(id, 30 + i);
            z.keyLow = z.keyHigh = 30 + i;   // one key per sample
            z.velLow = 1;
            z.velHigh = 127;
            r->map.zones.push_back(z);
        }
    }
    for (Rig* r : { &full, &st }) r->useMap();
    st.streamer.start();
    st.engine.setNonRealtime(true);
    int peakStreams = 0;
    auto play = [&](Rig& r, bool track) {
        return render(r.engine, 120, 512, [&](int block) {
            if (block < 32)
                for (int k = 0; k < 4; ++k)
                {
                    const int j = block * 4 + k;   // 128 distinct (key, channel) pairs
                    r.engine.noteOn(30 + j % kSamples, 60 + j % 60, 1 + j / kSamples);
                }
            if (track)
                peakStreams = std::max(peakStreams, r.streamer.activeStreams());
        });
    };
    const auto a = play(full, false);
    const auto b = play(st, true);
    CHECK(bitIdentical(a, b));
    CHECK_EQ(st.engine.underrunCount(), static_cast<uint64_t>(0));
    CHECK(peakStreams >= 100);
    CHECK(peakStreams <= st.streamer.config().numSlots);
    std::cout << "  peak concurrent streams: " << peakStreams << "\n";

    // Real-time paced: 32 voices, reader threads only (no blocking reads), 1.5 s of audio.
    StreamerConfig rt;
    rt.numThreads = 2;
    Rig live(rt, 64);
    for (int i = 0; i < 32; ++i)
    {
        const std::string id = "r" + std::to_string(i);
        live.pool.setBuffer(id, makeStreamedCopy(makeSignal(2, 44100.0, 2.0, static_cast<uint32_t>(i)), 32768));
        Zone z = fullZone(id, 40 + i);
        z.keyLow = z.keyHigh = 40 + i;
        live.map.zones.push_back(z);
    }
    live.useMap();
    live.streamer.start();
    std::vector<float> l(256), rr(256);
    for (int i = 0; i < 32; ++i)
        live.engine.noteOn(40 + i, 100, 1);  // root = key: ratio 1 (2x would also fit)
    const auto t0 = std::chrono::steady_clock::now();
    const int blocks = static_cast<int>(1.5 * 44100.0 / 256.0);
    for (int b = 0; b < blocks; ++b)
    {
        live.engine.processBlock(l.data(), rr.data(), 256);
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<long long>((b + 1) * 256.0 / 44100.0 * 1.0e6)));
    }
    std::cout << "  real-time paced: 32 streams, underruns " << live.engine.underrunCount() << "\n";
    CHECK_EQ(live.engine.underrunCount(), static_cast<uint64_t>(0));
    CHECK_EQ(live.engine.streamingStats().streamingVoices, 32);
}

static void testLiveSwapAndGarbageCollection()
{
    std::cout << "testLiveSwapAndGarbageCollection\n";
    Rig r;
    const auto a = makeSignal(2, 44100.0, 2.0, 21);
    const auto b = makeSignal(2, 44100.0, 2.0, 22);
    r.pool.setBuffer("x", makeStreamedCopy(a, 2048));
    r.map.zones = { fullZone("x") };
    r.useMap();
    r.engine.noteOn(60, 100, 1);
    render(r.engine, 4, 256, [&](int) { r.streamer.serviceUntilIdle(); });

    // Relocate / live swap while the note sounds: the voice keeps the old buffer and slot
    r.pool.setBuffer("x", makeStreamedCopy(b, 2048));
    CHECK_EQ(r.pool.retiredBufferCount(), static_cast<size_t>(1));
    r.pool.collectGarbage();
    CHECK_EQ(r.pool.retiredBufferCount(), static_cast<size_t>(1));      // still playing -> kept
    render(r.engine, 40, 256, [&](int) { r.streamer.serviceUntilIdle(); });
    CHECK_EQ(r.engine.underrunCount(), static_cast<uint64_t>(0));

    // New note-ons get the new audio
    r.engine.noteOn(62, 100, 1);
    const Voice* v = r.engine.findActiveVoice(62, 1);
    CHECK(v != nullptr && v->buffer.get() == r.pool.getBuffer("x").get());

    // Old voice ends -> old buffer (and the snapshot that held it) can be freed
    r.engine.allNotesOff();
    render(r.engine, 40, 256, [&](int) { r.streamer.serviceUntilIdle(); });
    r.engine.noteOn(64, 100, 1); // re-pins the current snapshot
    r.pool.collectGarbage();
    CHECK_EQ(r.pool.retiredBufferCount(), static_cast<size_t>(0));
    CHECK_EQ(r.pool.snapshotCount(), static_cast<size_t>(1));

    // Full RAM <-> streaming swap of the same id (the "Load fully into RAM" toggle)
    r.pool.setBuffer("x", b);
    r.engine.noteOn(65, 100, 1);
    const Voice* w = r.engine.findActiveVoice(65, 1);
    CHECK(w != nullptr && !w->streaming && w->streamSlot < 0);

    // Hazard pin: a snapshot an engine still uses is never freed under it
    SamplePool pool;
    const int reader = pool.registerReader();
    CHECK(reader >= 0);
    pool.setBuffer("k", a);
    const PoolSnapshot* pinned = pool.pin(reader);
    pool.setBuffer("k", b);
    pool.setBuffer("j", a);
    CHECK(pool.snapshotCount() >= 2);
    CHECK(pinned->find("k") != nullptr && pinned->find("j") == nullptr); // still readable
    const PoolSnapshot* now = pool.pin(reader);
    CHECK(now->find("j") != nullptr);
    pool.collectGarbage();
    CHECK_EQ(pool.snapshotCount(), static_cast<size_t>(1));
    pool.unregisterReader(reader);
}

static void testSmallSamplesStayInRam()
{
    std::cout << "testSmallSamplesStayInRam\n";
    Rig r;
    r.pool.setBuffer("tiny", makeStreamedCopy(makeSignal(2, 44100.0, 0.2), 65536));
    r.map.zones = { fullZone("tiny") };
    r.useMap();
    r.engine.noteOn(60, 100, 1);
    const Voice* v = r.engine.findActiveVoice(60, 1);
    CHECK(v != nullptr && !v->streaming && v->streamSlot < 0);
    CHECK_EQ(r.streamer.activeStreams(), 0);
    // Rings are only allocated when asked (fully-in-RAM patches pay nothing)
    DiskStreamer lazy(Rig::threadless());
    CHECK_EQ(lazy.ringBytes(), static_cast<size_t>(0));
    lazy.ensureRings();
    CHECK(lazy.ringBytes() > 0);
}

static void testSettingsPersistence()
{
    std::cout << "testSettingsPersistence\n";
    // Per patch: "Load fully into RAM" round-trips through .looper.json; old patches stream
    Patch p;
    p.name = "Big";
    p.map.zones = { fullZone("x") };
    p.map.loadIntoRam = true;
    const auto json = PatchStore::toJson(p);
    CHECK(json.find("\"loadIntoRam\":true") != std::string::npos);
    auto back = PatchStore::fromJson(json);
    CHECK(back.has_value() && back->map.loadIntoRam);
    p.map.loadIntoRam = false;
    back = PatchStore::fromJson(PatchStore::toJson(p));
    CHECK(back.has_value() && !back->map.loadIntoRam);
    std::string legacy = PatchStore::toJson(p);
    const auto at = legacy.find("\"loadIntoRam\":false,");
    CHECK(at != std::string::npos);
    if (at != std::string::npos)
        legacy.erase(at, std::string("\"loadIntoRam\":false,").size());
    back = PatchStore::fromJson(legacy);
    CHECK(back.has_value() && !back->map.loadIntoRam);

    // Session: preload size round-trips, is clamped, and defaults to 64k frames when absent
    SessionPrefs prefs;
    CHECK_EQ(prefs.preloadFrames, 65536);
    prefs.preloadFrames = 131072;
    auto restored = SessionPrefs::fromJson(prefs.toJson());
    CHECK(restored.has_value() && restored->preloadFrames == 131072);
    CHECK(restored->approximatelyEqual(prefs));
    auto tiny = SessionPrefs::fromJson("{\"preloadFrames\":10}");
    CHECK(tiny.has_value() && tiny->preloadFrames == SessionPrefs::kMinPreloadFrames);
    auto old = SessionPrefs::fromJson("{\"polyphony\":32}");
    CHECK(old.has_value() && old->preloadFrames == SessionPrefs::kDefaultPreloadFrames);
}

#if LOOPER_STREAMING_WITH_JUCE
static juce::File writeWav(const juce::String& name, int channels, int bits, double sr, double seconds, uint32_t seed)
{
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("looper-streaming-tests").getChildFile(name);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(channels, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-0.9f, 0.9f);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i)
            buf.setSample(c, i, 0.5f * std::sin(0.01f * (float) (i * (c + 1))) + 0.3f * noise(rng));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, (unsigned) channels, bits, {}, 0));
    if (w != nullptr)
    {
        os.release();
        w->writeFromAudioSampleBuffer(buf, 0, n);
    }
    return f;
}

static void testFileStreamingMatchesFullDecode()
{
    std::cout << "testFileStreamingMatchesFullDecode\n";
    ImportController ic;
    struct Case { const char* name; int ch; int bits; double sr; };
    for (const auto& c : { Case { "st24.wav", 2, 24, 44100.0 }, Case { "mono16.wav", 1, 16, 48000.0 },
                           Case { "quad24.wav", 4, 24, 44100.0 } })
    {
        const auto f = writeWav(c.name, c.ch, c.bits, c.sr, 2.5, 31);
        SampleBuffer full, streamed;
        juce::String err;
        ic.setStreamingOptions({ 65536, true });
        CHECK(ic.decodeFile(f, full, err));
        ic.setStreamingOptions({ 4096, false });
        CHECK(ic.decodeFile(f, streamed, err));
        CHECK(!full.isStreaming());
        CHECK(streamed.isStreaming());
        CHECK_EQ(streamed.residentLength(), static_cast<int64_t>(4096));
        CHECK_EQ(streamed.length, full.length);
        CHECK_EQ(streamed.channels, full.channels);
        std::vector<float> all(full.interleaved.size());
        CHECK_EQ(streamed.copyFrames(0, full.length, all.data()), full.length);
        CHECK(bitIdentical(all, full.interleaved));
        // Random-access reads (what the ring does after an underrun skip)
        std::mt19937 rng(5);
        for (int k = 0; k < 20; ++k)
        {
            const int64_t start = 4096 + static_cast<int64_t>(rng() % static_cast<uint32_t>(full.length - 9000));
            std::vector<float> part(static_cast<size_t>(5000 * full.channels));
            streamed.stream->readFrames(start, 5000, part.data());
            CHECK(std::equal(part.begin(), part.end(), full.interleaved.begin() + start * full.channels,
                             [](float x, float y) { return std::memcmp(&x, &y, sizeof(float)) == 0; }));
        }

        // Engine playback from the real file (offline = exact) matches the full decode
        Rig a, b(StreamerConfig {});
        a.pool.setBuffer("x", full);
        b.pool.setBuffer("x", streamed);
        b.streamer.start();
        for (Rig* r : { &a, &b })
        {
            r->map.zones = { fullZone("x") };
            r->useMap();
        }
        b.engine.setNonRealtime(true);
        auto play = [](Rig& r) {
            return render(r.engine, 150, 512, [&](int block) {
                if (block == 0) r.engine.noteOn(60, 100, 1);
                if (block == 9) r.engine.noteOn(79, 100, 1);
            });
        };
        CHECK(bitIdentical(play(a), play(b)));
        CHECK_EQ(b.engine.underrunCount(), static_cast<uint64_t>(0));
    }

    // Open-reader cap: many streamed files never keep more than the cap open
    FileStreamSource::setMaxOpenReaders(8);
    std::vector<std::shared_ptr<FileStreamSource>> sources;
    for (int i = 0; i < 20; ++i)
    {
        const auto f = writeWav("many" + juce::String(i) + ".wav", 1, 16, 44100.0, 0.3, (uint32_t) i);
        sources.push_back(std::make_shared<FileStreamSource>(f, 1, (int64_t) (44100 * 0.3)));
        std::vector<float> tmp(1000);
        CHECK_EQ(sources.back()->readFrames(100, 1000, tmp.data()), static_cast<int64_t>(1000));
        CHECK(FileStreamSource::openReaderCount() <= 8);
    }
    // Evicted readers reopen transparently
    std::vector<float> tmp(500);
    CHECK_EQ(sources.front()->readFrames(0, 500, tmp.data()), static_cast<int64_t>(500));
    sources.clear();
    CHECK_EQ(FileStreamSource::openReaderCount(), 0);
    FileStreamSource::setMaxOpenReaders(48);
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("looper-streaming-tests").deleteRecursively();
}
#endif

int main()
{
    testBufferBasics();
    testMemoryFormat();
    testBitExactPitchRatios();
    testBoundaryCrossing();
    testUnderrunFadesAndCounts();
    testSlowDiskWithThreads();
    testVoiceStealFreesSlots();
    testOfflineNeverUnderruns();
    testNoAllocationOnAudioThread();
    testManyConcurrentVoices();
    testLiveSwapAndGarbageCollection();
    testSmallSamplesStayInRam();
    testSettingsPersistence();
#if LOOPER_STREAMING_WITH_JUCE
    testFileStreamingMatchesFullDecode();
#else
    std::cout << "(file streaming tests skipped: built without JUCE)\n";
#endif
    std::cout << "\nStreamingTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
