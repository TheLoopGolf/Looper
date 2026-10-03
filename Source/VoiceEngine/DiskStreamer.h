#pragma once

#include "../SamplePool/SamplePool.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace looper {

/** Sizing for DiskStreamer. */
struct StreamerConfig
{
    /** Concurrent streams (voices past their preload). Polyphony max 128 + headroom for retiring slots. */
    int numSlots = 160;
    /** Per-slot ring capacity in frames (rounded up to a power of two). */
    int ringFrames = 16384;
    /** Largest single disk read in frames. */
    int chunkFrames = 8192;
    /** Background reader threads; 0 = none (tests call serviceOnce() themselves). */
    int numThreads = 2;
};

/**
 * Disk streaming for samples longer than their RAM preload.
 *
 * Each voice that plays past its preload owns a stream slot: a fixed, pre-allocated single
 * producer / single consumer ring of frames. Background reader threads (the producers) keep each
 * ring filled ahead of the voice's read position, most urgent first (frames buffered divided by
 * the voice's playback rate, so a voice pitched up two octaves is serviced four times sooner).
 * The audio thread (the consumer) only does atomic loads/stores and CAS on slot state: it never
 * locks, allocates, frees or touches the disk in real-time mode.
 *
 * Ring layout: absolute frame f of the sample lives at ring[(f & mask) * channels]. Frames
 * [validFrom, writeEnd) are readable, and the producer never writes past readFloor + capacity,
 * so frames the consumer still needs are never overwritten. A consumer that ran dry (underrun)
 * publishes a floor beyond writeEnd; the producer then skips ahead to that floor so the voice
 * can resume instead of chasing stale audio.
 *
 * Slot state machine (all transitions by CAS, so release from any thread is safe):
 *   Free -> Claimed -> Active            acquire (audio thread)
 *   Active -> Busy -> Active             a reader thread filling the ring
 *   Active -> Claimed -> Free            release while idle
 *   Busy -> BusyRetiring -> Free         release while a reader is mid-fill (it frees the slot)
 */
class DiskStreamer
{
public:
    explicit DiskStreamer(StreamerConfig config = {});
    ~DiskStreamer();

    DiskStreamer(const DiskStreamer&) = delete;
    DiskStreamer& operator=(const DiskStreamer&) = delete;

    /** Message thread: start / stop the reader threads (idempotent). */
    void start();
    void stop();
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    /**
     * Message thread: allocate the rings (done lazily, the first time a streamed sample is
     * loaded, so fully-in-RAM patches pay nothing). Must happen before any acquire() can return
     * a slot; acquire() fails (voice plays its preload only) until then.
     */
    void ensureRings();
    bool ringsAllocated() const noexcept { return ringsReady_.load(std::memory_order_acquire); }

    const StreamerConfig& config() const noexcept { return config_; }
    int ringCapacity() const noexcept { return ringFrames_; }

    // --- Audio thread -----------------------------------------------------------------------
    /**
     * Claim a slot streaming `buffer` from `firstFrame` (clamped to its resident length).
     * Copies the shared_ptr (atomic increment only). -1 if every slot is busy.
     */
    int acquire(const std::shared_ptr<const SampleBuffer>& buffer, int64_t firstFrame) noexcept;
    /** A voice wanted a slot and none was free (counted for the UI; acquire() does not count). */
    void noteExhausted() noexcept { slotsExhausted_.fetch_add(1, std::memory_order_relaxed); }
    /** Give a slot back (voice ended / stolen). Any thread. */
    void release(int slot) noexcept;

    /** Readable frame window of a slot: frames [from, end), frame f at ring[(f & mask) * channels]. */
    struct Window
    {
        const float* ring = nullptr;
        int64_t from = 0;
        int64_t end = 0;
        int64_t mask = 0;
        int channels = 1;
        bool contains(int64_t f) const noexcept { return f >= from && f < end; }
        const float* frame(int64_t f) const noexcept { return ring + (f & mask) * channels; }
    };
    Window window(int slot) const noexcept;

    /**
     * Consumer progress: frames below `floorFrame` are no longer needed; `framesPerSample` is the
     * voice's current playback rate (pitch ratio x file/host rate), used to prioritise refills.
     */
    void publishProgress(int slot, int64_t floorFrame, double framesPerSample) noexcept;

    /**
     * Offline / non-realtime rendering: make frames up to `endFrame` readable, filling the ring
     * on the calling thread if needed (blocks on disk I/O). False if impossible (slot not active,
     * read error, or the span does not fit in the ring).
     */
    bool fillBlocking(int slot, int64_t floorFrame, int64_t endFrame) noexcept;

    // --- Reader threads / tests --------------------------------------------------------------
    /** Fill the most urgent slot by one chunk. True if any work was done. */
    bool serviceOnce() noexcept;
    /** Keep calling serviceOnce() until no slot needs data (tests). */
    void serviceUntilIdle() noexcept
    {
        while (serviceOnce()) {}
    }
    /** Test hook: reader threads stop servicing (simulates a stalled disk). */
    void setPaused(bool paused) noexcept { paused_.store(paused, std::memory_order_release); }

    // --- Stats (any thread) -------------------------------------------------------------------
    int activeStreams() const noexcept;
    /** Slots fully free (not active, filling or retiring). */
    int freeSlots() const noexcept;
    uint64_t slotsExhausted() const noexcept { return slotsExhausted_.load(std::memory_order_relaxed); }
    uint64_t readErrors() const noexcept { return readErrors_.load(std::memory_order_relaxed); }
    uint64_t framesStreamed() const noexcept { return framesStreamed_.load(std::memory_order_relaxed); }
    size_t ringBytes() const noexcept;

private:
    enum State : int { Free = 0, Claimed, Active, Busy, BusyRetiring };

    struct Slot
    {
        std::atomic<int> state { Free };
        std::shared_ptr<const SampleBuffer> buffer; // owned by whoever holds Claimed / Busy
        std::atomic<int64_t> writeEnd { 0 };
        std::atomic<int64_t> validFrom { 0 };
        std::atomic<int64_t> readFloor { 0 };
        std::atomic<double> rate { 1.0 };
        std::atomic<bool> failed { false };
        std::atomic<int> channels { 1 };
        std::atomic<int64_t> length { 0 };
        float* ring = nullptr;   // set once by ensureRings(), before any slot can go Active
    };

    /** Caller holds the slot in Busy. Reads one chunk; returns frames written (0 = nothing to do). */
    int64_t fillSlot(Slot& s) noexcept;
    /** Leave Busy: back to Active, or free the slot if it was released meanwhile. */
    void endBusy(Slot& s) noexcept;
    bool needsFill(const Slot& s, double& urgency) const noexcept;
    void workerLoop();

    StreamerConfig config_;
    int ringFrames_ = 16384;
    int64_t mask_ = 16383;
    std::vector<Slot> slots_;
    std::vector<float> ringStorage_;
    std::mutex ringMutex_;
    std::atomic<bool> ringsReady_ { false };
    std::atomic<uint32_t> acquireHint_ { 0 };

    std::vector<std::thread> threads_;
    std::atomic<bool> running_ { false };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> paused_ { false };

    std::atomic<uint64_t> slotsExhausted_ { 0 };
    std::atomic<uint64_t> readErrors_ { 0 };
    std::atomic<uint64_t> framesStreamed_ { 0 };
};

} // namespace looper
