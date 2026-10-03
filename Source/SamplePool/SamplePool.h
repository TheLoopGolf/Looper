#pragma once

#include "../InstrumentMap/InstrumentMap.h"
#include "SampleStream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace looper {

struct SampleBuffer
{
    /**
     * Resident audio, mono or stereo interleaved. Holds the whole sample when it is fully in
     * RAM, or only the first `residentFrames` frames (the preload) when the rest streams from
     * `stream` (disk streaming, see DiskStreamer).
     */
    std::vector<float> interleaved;
    int channels = 1;
    double sampleRate = 44100.0;
    int64_t length = 0;          // total frames of the sample (resident + streamed)
    /** Frames held in `interleaved`. 0 = derive from interleaved.size() (SamplePool normalises). */
    int64_t residentFrames = 0;
    /** Remainder source when residentFrames < length; nullptr = fully in RAM. */
    std::shared_ptr<SampleStreamSource> stream;

    bool isStreaming() const noexcept { return stream != nullptr && residentFrames < length; }
    /** Frames playable straight from RAM. */
    int64_t residentLength() const noexcept
    {
        const int64_t held = static_cast<int64_t>(interleaved.size()) / std::max(1, channels);
        const int64_t r = residentFrames > 0 ? std::min(residentFrames, held) : held;
        return std::min(r, length);
    }
    size_t residentBytes() const noexcept { return interleaved.capacity() * sizeof(float); }
    /** Bytes the sample would take fully decoded in RAM. */
    size_t fullBytes() const noexcept
    {
        return static_cast<size_t>(std::max<int64_t>(0, length)) * static_cast<size_t>(std::max(1, channels)) * sizeof(float);
    }

    /**
     * Copy frames [start, start + frames) into `out` (interleaved), reading the resident part
     * from RAM and the rest from `stream` (blocking). Message / worker threads only: used for
     * pitch analysis of streamed samples and by tests. Returns frames copied.
     */
    int64_t copyFrames(int64_t start, int64_t frames, float* out) const
    {
        const int ch = std::max(1, channels);
        start = std::max<int64_t>(0, start);
        frames = std::max<int64_t>(0, std::min(frames, length - start));
        const int64_t res = residentLength();
        int64_t done = 0;
        if (start < res)
        {
            done = std::min(frames, res - start);
            std::copy(interleaved.begin() + static_cast<std::ptrdiff_t>(start * ch),
                      interleaved.begin() + static_cast<std::ptrdiff_t>((start + done) * ch), out);
        }
        if (done < frames && stream)
            done += stream->readFrames(start + done, frames - done, out + done * ch);
        return done;
    }
};

/** Make `buf` consistent: length >= resident frames, residentFrames set, stream dropped if unused. */
inline void normaliseSampleBuffer(SampleBuffer& buf)
{
    buf.channels = std::clamp(buf.channels, 1, 2);
    const int64_t held = static_cast<int64_t>(buf.interleaved.size()) / buf.channels;
    if (buf.residentFrames <= 0 || buf.residentFrames > held)
        buf.residentFrames = held;
    if (!buf.stream)
        buf.length = buf.residentFrames;           // no remainder source: RAM is all there is
    else if (buf.length <= buf.residentFrames)
    {
        buf.length = buf.residentFrames;
        buf.stream.reset();                         // short sample: fully in RAM after all
    }
}

/**
 * Split a fully decoded buffer into a `preloadFrames` RAM head + in-memory stream source for the
 * rest (tests, benchmarks). Buffers no longer than the preload are returned fully resident.
 */
inline SampleBuffer makeStreamedCopy(const SampleBuffer& full, int64_t preloadFrames)
{
    SampleBuffer out;
    out.channels = std::max(1, full.channels);
    out.sampleRate = full.sampleRate;
    out.length = full.length > 0 ? full.length : static_cast<int64_t>(full.interleaved.size()) / out.channels;
    const int64_t pre = std::clamp<int64_t>(preloadFrames, 1, std::max<int64_t>(1, out.length));
    if (pre >= out.length)
    {
        out.interleaved = full.interleaved;
        out.residentFrames = out.length;
        return out;
    }
    out.interleaved.assign(full.interleaved.begin(), full.interleaved.begin() + static_cast<std::ptrdiff_t>(pre * out.channels));
    out.residentFrames = pre;
    out.stream = std::make_shared<MemoryStreamSource>(std::make_shared<const std::vector<float>>(full.interleaved), out.channels);
    return out;
}

inline constexpr const char* kDemoSampleId = "demo";

/**
 * ~0.4 s band-limited-ish decaying sine at C4 (261.6256 Hz), mono.
 * Used so Standalone makes sound without external files.
 */
inline SampleBuffer makeDemoToneBuffer(double sampleRate = 44100.0, double durationSec = 0.4)
{
    SampleBuffer buf;
    buf.channels = 1;
    buf.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int64_t n = static_cast<int64_t>(std::max(1.0, buf.sampleRate * durationSec));
    buf.length = n;
    buf.interleaved.resize(static_cast<size_t>(n));

    constexpr double kC4Hz = 261.6255653;
    const double twoPi = 6.283185307179586;
    // Soft attack (~3 ms) + exponential-ish decay envelope on the tone itself
    const double attackFrames = std::max(1.0, buf.sampleRate * 0.003);

    for (int64_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / buf.sampleRate;
        const double phase = twoPi * kC4Hz * t;
        // Simple band-limit-ish: fundamental + weak 2nd/3rd harmonics below Nyquist
        double s = std::sin(phase);
        s += 0.25 * std::sin(2.0 * phase);
        s += 0.08 * std::sin(3.0 * phase);
        const double attack = std::min(1.0, static_cast<double>(i) / attackFrames);
        const double decay = std::exp(-4.5 * t / durationSec);
        buf.interleaved[static_cast<size_t>(i)] = static_cast<float>(0.35 * s * attack * decay);
    }
    return buf;
}

/** Immutable id -> buffer table the audio thread reads (published by SamplePool). */
struct PoolSnapshot
{
    std::unordered_map<std::string, std::shared_ptr<const SampleBuffer>> buffers;

    /** Audio thread: no allocation, no locks (std::string key lookup only hashes). */
    const std::shared_ptr<const SampleBuffer>* find(const std::string& id) const noexcept
    {
        const auto it = buffers.find(id);
        return it == buffers.end() ? nullptr : &it->second;
    }
};

/** RAM accounting for the UI ("RAM 42 MB . Streaming"). */
struct PoolMemoryStats
{
    size_t residentBytes = 0;   // decoded audio actually held in RAM (current buffers)
    size_t fullBytes = 0;       // what the same samples would take fully loaded
    int samples = 0;
    int streamingSamples = 0;   // samples whose tail streams from disk
};

/**
 * Sample pool: decoded buffers keyed by sample id.
 *
 * Writers (message thread: import, patch load, relocation, streaming-mode reloads) take a mutex
 * and publish an immutable PoolSnapshot. The audio thread never locks: it pins the current
 * snapshot through a per-reader hazard pointer (pin()), looks a buffer up and copies its
 * shared_ptr (an atomic increment). Nothing is ever freed on the audio thread: replaced buffers
 * and old snapshots are kept until collectGarbage() (message thread) sees nobody uses them.
 */
class SamplePool
{
public:
    static constexpr int kMaxReaders = 32;

    SamplePool()
    {
        for (auto& h : hazards_) h.store(nullptr, std::memory_order_relaxed);
        for (auto& u : readerUsed_) u.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        publishLocked();
    }

    ~SamplePool()
    {
        live_.store(nullptr);
    }

    SamplePool(const SamplePool&) = delete;
    SamplePool& operator=(const SamplePool&) = delete;

    bool contains(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffers_.count(id) > 0;
    }

    void setBuffer(const std::string& id, SampleBuffer buffer)
    {
        setBuffer(id, std::make_shared<SampleBuffer>(std::move(buffer)));
    }

    void setBuffer(const std::string& id, std::shared_ptr<SampleBuffer> buffer)
    {
        if (!buffer)
            return;
        if (buffer->length <= 0 && !buffer->interleaved.empty() && !buffer->stream)
            buffer->length = static_cast<int64_t>(buffer->interleaved.size() / static_cast<size_t>(std::max(1, buffer->channels)));
        normaliseSampleBuffer(*buffer);
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = buffers_[id];
        if (slot)
            retired_.push_back(std::move(slot)); // voices may still play it; freed by GC later
        slot = std::move(buffer);
        if (batchDepth_ == 0)
            publishLocked();
        else
            dirty_ = true;
    }

    /** Message thread. Locks; the audio thread uses pin() instead. */
    std::shared_ptr<const SampleBuffer> getBuffer(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = buffers_.find(id);
        if (it == buffers_.end())
            return nullptr;
        return it->second;
    }

    SampleRef* addStub(SampleRef ref)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto id = ref.id;
        refs_[id] = std::move(ref);
        return &refs_[id];
    }

    /** Message thread only (pointer stays valid until the id is re-stubbed / cleared). */
    const SampleRef* getRef(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = refs_.find(id);
        return it == refs_.end() ? nullptr : &it->second;
    }

    /** Install demo tone buffer under kDemoSampleId (overwrites if present). */
    void loadDemoSample(double sampleRate = 44100.0)
    {
        auto buf = makeDemoToneBuffer(sampleRate);
        SampleRef ref;
        ref.id = kDemoSampleId;
        ref.path = "demo://tone-c4";
        ref.displayName = "Demo C4 Tone";
        ref.durationSamples = buf.length;
        ref.sampleRate = buf.sampleRate;
        ref.channels = buf.channels;
        ref.detectedRootKey = 60;
        addStub(std::move(ref));
        setBuffer(kDemoSampleId, std::move(buf));
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refs_.clear();
        for (auto& kv : buffers_)
            retired_.push_back(std::move(kv.second));
        buffers_.clear();
        publishLocked();
    }

    // --- Batching (import / patch load: publish one snapshot instead of one per file) -----
    void beginBatch()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++batchDepth_;
    }
    void endBatch()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (batchDepth_ > 0 && --batchDepth_ == 0 && dirty_)
            publishLocked();
    }
    struct ScopedBatch
    {
        explicit ScopedBatch(SamplePool& p) : pool(p) { pool.beginBatch(); }
        ~ScopedBatch() { pool.endBatch(); }
        SamplePool& pool;
    };

    // --- Lock-free audio-thread read path ------------------------------------------------
    /** Message thread (VoiceEngine::setSamplePool): claim a hazard slot. -1 if none left. */
    int registerReader() noexcept
    {
        for (int i = 0; i < kMaxReaders; ++i)
        {
            bool expected = false;
            if (readerUsed_[static_cast<size_t>(i)].compare_exchange_strong(expected, true))
            {
                hazards_[static_cast<size_t>(i)].store(nullptr);
                return i;
            }
        }
        return -1;
    }

    void unregisterReader(int reader) noexcept
    {
        if (reader < 0 || reader >= kMaxReaders)
            return;
        hazards_[static_cast<size_t>(reader)].store(nullptr);
        readerUsed_[static_cast<size_t>(reader)].store(false);
    }

    /**
     * Audio thread, lock-free: the current snapshot, protected until this reader pins again
     * (or unregisters). Never nullptr while the pool is alive.
     */
    const PoolSnapshot* pin(int reader) const noexcept
    {
        if (reader < 0 || reader >= kMaxReaders)
            return nullptr;
        auto& hazard = hazards_[static_cast<size_t>(reader)];
        const PoolSnapshot* p = live_.load();
        for (;;)
        {
            hazard.store(p);
            const PoolSnapshot* again = live_.load();
            if (again == p)
                return p;
            p = again;
        }
    }

    /** Message thread: free old snapshots no reader pins and replaced buffers nobody plays. */
    void collectGarbage()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        collectGarbageLocked();
    }

    /** Buffers replaced or cleared but still referenced (e.g. by a sounding voice). Tests / UI. */
    size_t retiredBufferCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return retired_.size();
    }

    size_t snapshotCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_.size();
    }

    PoolMemoryStats memoryStats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        PoolMemoryStats st;
        for (const auto& kv : buffers_)
        {
            if (!kv.second)
                continue;
            if (kv.first != kDemoSampleId) // the built-in test tone is not a user sample
                ++st.samples;
            st.residentBytes += kv.second->residentBytes();
            st.fullBytes += kv.second->fullBytes();
            if (kv.second->isStreaming())
                ++st.streamingSamples;
        }
        return st;
    }

private:
    void publishLocked()
    {
        auto snap = std::make_unique<PoolSnapshot>();
        snap->buffers.reserve(buffers_.size());
        for (const auto& kv : buffers_)
            snap->buffers.emplace(kv.first, kv.second);
        live_.store(snap.get());
        snapshots_.push_back(std::move(snap));
        dirty_ = false;
        collectGarbageLocked();
    }

    void collectGarbageLocked()
    {
        const PoolSnapshot* live = live_.load();
        auto pinned = [this](const PoolSnapshot* s) {
            for (const auto& h : hazards_)
                if (h.load() == s)
                    return true;
            return false;
        };
        snapshots_.erase(std::remove_if(snapshots_.begin(), snapshots_.end(),
                                        [&](const std::unique_ptr<PoolSnapshot>& s) {
                                            return s.get() != live && !pinned(s.get());
                                        }),
                         snapshots_.end());
        // After the snapshots: a retired buffer only we hold can no longer be reached.
        retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                      [](const std::shared_ptr<const SampleBuffer>& b) { return !b || b.use_count() == 1; }),
                       retired_.end());
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SampleRef> refs_;
    std::unordered_map<std::string, std::shared_ptr<const SampleBuffer>> buffers_;
    std::vector<std::shared_ptr<const SampleBuffer>> retired_;
    std::vector<std::unique_ptr<PoolSnapshot>> snapshots_;
    std::atomic<const PoolSnapshot*> live_ { nullptr };
    mutable std::array<std::atomic<const PoolSnapshot*>, kMaxReaders> hazards_;
    std::array<std::atomic<bool>, kMaxReaders> readerUsed_;
    int batchDepth_ = 0;
    bool dirty_ = false;
};

/** Single full-range zone pointing at the demo sample (root C4=60). */
inline Zone makeDemoZone()
{
    Zone z;
    z.sampleId = kDemoSampleId;
    z.rootKey = 60;
    z.keyLow = 0;
    z.keyHigh = 127;
    z.velLow = 1;
    z.velHigh = 127;
    return z;
}

} // namespace looper
