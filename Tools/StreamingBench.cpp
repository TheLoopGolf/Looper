// Rough disk-streaming benchmark (not a unit test; built with -DLOOPER_BUILD_BENCH=ON).
//
// Generates a large library of stereo 24-bit WAVs, then loads it twice through the real
// ImportController path, fully into RAM and streamed (64k-frame preload), reporting load time and
// RAM. Then plays 64 voices: offline (blocking reads, as fast as possible) and real-time paced
// (reader threads only), reporting underruns and audio-thread cost per block.
//
// Usage: StreamingBench [libraryDir] [numFiles] [secondsPerFile]

#include "Import/ImportController.h"
#include "SamplePool/MemoryFormat.h"
#include "SamplePool/SamplePool.h"
#include "VoiceEngine/DiskStreamer.h"
#include "VoiceEngine/VoiceEngine.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <thread>

#if defined(__linux__)
 #include <fcntl.h>
 #include <unistd.h>
#endif

using namespace looper;
using Clock = std::chrono::steady_clock;

static double secondsSince(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

/** Ask the OS to drop a file from the page cache (Linux; best effort) for cold-ish loads. */
static void evictFromCache(const juce::File& f)
{
#if defined(__linux__)
    const int fd = ::open(f.getFullPathName().toRawUTF8(), O_RDONLY);
    if (fd >= 0)
    {
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        ::close(fd);
    }
#else
    (void) f;
#endif
}

static long rssKb()
{
#if defined(__linux__)
    FILE* fp = std::fopen("/proc/self/status", "r");
    if (!fp) return -1;
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof(line), fp))
        if (std::sscanf(line, "VmRSS: %ld kB", &kb) == 1)
            break;
    std::fclose(fp);
    return kb;
#else
    return -1;
#endif
}

static juce::Array<juce::File> makeLibrary(const juce::File& dir, int numFiles, double seconds)
{
    dir.createDirectory();
    juce::Array<juce::File> files;
    const double sr = 44100.0;
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    for (int k = 0; k < numFiles; ++k)
    {
        auto f = dir.getChildFile("Pad " + juce::String(k).paddedLeft('0', 3) + ".wav");
        files.add(f);
        if (f.existsAsFile() && f.getSize() > (juce::int64) n * 6)
            continue;
        std::mt19937 rng((uint32_t) k);
        std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
        const double hz = 55.0 * std::pow(2.0, k / 12.0);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const float s = (float) (0.4 * std::sin(6.283185307179586 * hz * t) * std::exp(-t * 0.1));
            buf.setSample(0, i, s + 0.02f * noise(rng));
            buf.setSample(1, i, s * 0.9f + 0.02f * noise(rng));
        }
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 2, 24, {}, 0));
        if (w)
        {
            os.release();
            w->writeFromAudioSampleBuffer(buf, 0, n);
        }
    }
    return files;
}

struct LoadResult { double seconds = 0; PoolMemoryStats stats; long rssDeltaKb = 0; };

static LoadResult loadAll(ImportController& ic, SamplePool& pool, const juce::Array<juce::File>& files, StreamingOptions opts)
{
    for (const auto& f : files) evictFromCache(f);
    ic.setStreamingOptions(opts);
    const long rss0 = rssKb();
    const auto t0 = Clock::now();
    {
        SamplePool::ScopedBatch batch(pool);
        for (const auto& f : files)
        {
            SampleRef ref;
            ref.id = "f" + std::to_string(files.indexOf(f));
            ref.displayName = f.getFileName().toStdString();
            ic.loadFileIntoPool(f, pool, ref);
        }
    }
    LoadResult r;
    r.seconds = secondsSince(t0);
    r.stats = pool.memoryStats();
    r.rssDeltaKb = rssKb() - rss0;
    return r;
}

struct PlayResult { double renderSeconds = 0; double audioSeconds = 0; uint64_t underruns = 0; double usPerBlock = 0; int peakStreams = 0; };

static PlayResult play(SamplePool& pool, DiskStreamer* streamer, int numFiles, bool offline, bool paced, double audioSeconds)
{
    VoiceEngine engine;
    engine.setSamplePool(&pool);
    engine.setStreamer(streamer);
    engine.setSampleRate(44100.0);
    engine.setPolyphony(64);
    AmpEnv::Params env;
    env.attackMs = 2.0f; env.decayMs = 100.0f; env.sustain = 1.0f; env.releaseMs = 200.0f;
    engine.setEnvParams(env);
    engine.setNonRealtime(offline);
    InstrumentMap map;
    for (int k = 0; k < numFiles; ++k)
    {
        Zone z;
        z.sampleId = "f" + std::to_string(k);
        z.keyLow = z.keyHigh = 24 + (k % 80);
        z.rootKey = z.keyLow - 12 * (k % 3);  // pitch ratios 1, 2, 4 (up to +2 octaves)
        z.rrGroup = 0;
        if (k < 80) map.zones.push_back(z);
    }
    engine.setMap(&map);
    const int block = 256;
    const int blocks = (int) (audioSeconds * 44100.0 / block);
    std::vector<float> l(block), r(block);
    PlayResult res;
    double busy = 0.0;
    const auto t0 = Clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        if (b < 64)
            engine.noteOn(24 + (b * 7) % std::min(80, numFiles), 100, 1);
        const auto s = Clock::now();
        engine.processBlock(l.data(), r.data(), block);
        busy += secondsSince(s);
        if (streamer) res.peakStreams = std::max(res.peakStreams, streamer->activeStreams());
        if (paced)
            std::this_thread::sleep_until(t0 + std::chrono::microseconds((long long) ((b + 1) * block / 44100.0 * 1e6)));
    }
    res.renderSeconds = secondsSince(t0);
    res.audioSeconds = blocks * block / 44100.0;
    res.underruns = engine.underrunCount();
    res.usPerBlock = busy / blocks * 1e6;
    return res;
}

int main(int argc, char** argv)
{
    const juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::String("/tmp/looper-bench-lib"));
    const int numFiles = argc > 2 ? std::atoi(argv[2]) : 96;
    const double seconds = argc > 3 ? std::atof(argv[3]) : 20.0;

    std::cout << "Generating " << numFiles << " x " << seconds << " s stereo 24-bit 44.1 kHz WAVs in "
              << dir.getFullPathName() << " ...\n";
    auto t0 = Clock::now();
    const auto files = makeLibrary(dir, numFiles, seconds);
    juce::int64 diskBytes = 0;
    for (const auto& f : files) diskBytes += f.getSize();
    std::cout << "  " << formatBytes((size_t) diskBytes) << " on disk (" << secondsSince(t0) << " s)\n\n";

    ImportController ic;

    // --- Full RAM ---
    SamplePool fullPool;
    const auto full = loadAll(ic, fullPool, files, { 65536, true });
    // --- Streaming ---
    SamplePool streamPool;
    DiskStreamer streamer;
    streamer.ensureRings();
    streamer.start();
    const auto stream = loadAll(ic, streamPool, files, { 65536, false });

    std::printf("| Mode | Load time (cold cache) | Sample RAM | + stream rings | RSS delta |\n");
    std::printf("|---|---|---|---|---|\n");
    std::printf("| Load fully into RAM | %.2f s | %s | - | %s |\n", full.seconds,
                formatBytes(full.stats.residentBytes).c_str(), formatBytes((size_t) std::max(0L, full.rssDeltaKb) * 1024).c_str());
    std::printf("| Streaming (64k preload) | %.2f s | %s | %s | %s |\n", stream.seconds,
                formatBytes(stream.stats.residentBytes).c_str(), formatBytes(streamer.ringBytes()).c_str(),
                formatBytes((size_t) std::max(0L, stream.rssDeltaKb) * 1024).c_str());
    std::printf("\n%d samples, %d streamed; fully decoded size %s\n\n", stream.stats.samples,
                stream.stats.streamingSamples, formatBytes(stream.stats.fullBytes).c_str());

    // --- Playback: 64 voices, ratios 1/2/4 ---
    for (const auto& f : files) evictFromCache(f);
    const auto offline = play(streamPool, &streamer, numFiles, true, false, 10.0);
    for (const auto& f : files) evictFromCache(f);
    const auto paced = play(streamPool, &streamer, numFiles, false, true, 6.0);
    const auto ram = play(fullPool, nullptr, numFiles, false, false, 10.0);
    std::printf("| Playback (64 voices, ratios 1/2/4) | Audio | Wall time | Audio-thread cost / 256-frame block | Underruns | Peak streams |\n");
    std::printf("|---|---|---|---|---|---|\n");
    std::printf("| Fully in RAM | %.1f s | %.2f s | %.1f us | - | - |\n", ram.audioSeconds, ram.renderSeconds, ram.usPerBlock);
    std::printf("| Streaming, offline bounce (blocking reads, cold cache) | %.1f s | %.2f s | %.1f us | %llu | %d |\n",
                offline.audioSeconds, offline.renderSeconds, offline.usPerBlock, (unsigned long long) offline.underruns, offline.peakStreams);
    std::printf("| Streaming, real-time paced (reader threads, cold cache) | %.1f s | %.2f s | %.1f us | %llu | %d |\n",
                paced.audioSeconds, paced.renderSeconds, paced.usPerBlock, (unsigned long long) paced.underruns, paced.peakStreams);
    std::printf("\nframes streamed: %llu, read errors: %llu\n", (unsigned long long) streamer.framesStreamed(),
                (unsigned long long) streamer.readErrors());
    streamer.stop();
    return 0;
}
