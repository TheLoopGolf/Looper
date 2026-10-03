#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace looper {

/**
 * Where the non-resident remainder of a streamed sample comes from (JUCE-free interface).
 *
 * The disk-streaming worker threads call readFrames() concurrently for different voices (and,
 * in offline / non-realtime rendering, the audio thread may call it too), so implementations
 * must be thread-safe. The real-time audio thread never calls it in real-time mode.
 */
class SampleStreamSource
{
public:
    virtual ~SampleStreamSource() = default;

    /**
     * Read `frames` frames starting at absolute frame `start` into `out`, interleaved with the
     * owning SampleBuffer's channel count (1 or 2). Returns the number of frames written; fewer
     * than requested only at the end of the file or on an I/O error.
     */
    virtual int64_t readFrames(int64_t start, int64_t frames, float* out) noexcept = 0;
};

/**
 * In-memory stream source over a fully decoded interleaved buffer. Used by the unit tests (with
 * optional artificial latency / stalls to simulate a slow disk) and the demo tone.
 */
class MemoryStreamSource : public SampleStreamSource
{
public:
    MemoryStreamSource(std::shared_ptr<const std::vector<float>> interleaved, int channels)
        : data_(std::move(interleaved)), channels_(std::max(1, channels)) {}

    int64_t readFrames(int64_t start, int64_t frames, float* out) noexcept override
    {
        reads_.fetch_add(1, std::memory_order_relaxed);
        while (stalled_.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (const int us = delayMicros_.load(std::memory_order_relaxed); us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(us));
        if (failing_.load(std::memory_order_relaxed) || !data_ || start < 0 || frames <= 0)
            return 0;
        const int64_t total = static_cast<int64_t>(data_->size()) / channels_;
        const int64_t n = std::max<int64_t>(0, std::min(frames, total - start));
        if (n > 0)
            std::memcpy(out, data_->data() + start * channels_,
                        static_cast<size_t>(n * channels_) * sizeof(float));
        return n;
    }

    /** Test hooks: slow every read down / block reads until un-stalled / make reads fail. */
    void setDelayMicros(int us) noexcept { delayMicros_.store(us, std::memory_order_relaxed); }
    void setStalled(bool s) noexcept { stalled_.store(s, std::memory_order_release); }
    void setFailing(bool f) noexcept { failing_.store(f, std::memory_order_relaxed); }
    long readCount() const noexcept { return reads_.load(std::memory_order_relaxed); }

private:
    std::shared_ptr<const std::vector<float>> data_;
    int channels_ = 1;
    std::atomic<int> delayMicros_ { 0 };
    std::atomic<bool> stalled_ { false };
    std::atomic<bool> failing_ { false };
    std::atomic<long> reads_ { 0 };
};

} // namespace looper
