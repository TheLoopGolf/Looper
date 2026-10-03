#include "DiskStreamer.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace looper {

namespace {
int roundUpPow2(int n)
{
    int p = 1;
    while (p < n && p < (1 << 24))
        p <<= 1;
    return p;
}
} // namespace

DiskStreamer::DiskStreamer(StreamerConfig config)
    : config_(config),
      slots_(static_cast<size_t>(std::clamp(config.numSlots, 1, 1024)))
{
    ringFrames_ = roundUpPow2(std::max(1024, config.ringFrames));
    mask_ = ringFrames_ - 1;
    config_.ringFrames = ringFrames_;
    config_.numSlots = static_cast<int>(slots_.size());
    config_.chunkFrames = std::clamp(config.chunkFrames, 256, ringFrames_ / 2);
}

DiskStreamer::~DiskStreamer()
{
    stop();
    for (auto& s : slots_)
        s.buffer.reset();
}

void DiskStreamer::ensureRings()
{
    std::lock_guard<std::mutex> lock(ringMutex_);
    if (ringsReady_.load(std::memory_order_acquire))
        return;
    // Always room for stereo; mono slots use the first half of their stride.
    const size_t stride = static_cast<size_t>(ringFrames_) * 2;
    ringStorage_.assign(stride * slots_.size(), 0.0f);
    for (size_t i = 0; i < slots_.size(); ++i)
        slots_[i].ring = ringStorage_.data() + i * stride;
    ringsReady_.store(true, std::memory_order_release);
}

size_t DiskStreamer::ringBytes() const noexcept
{
    return ringsAllocated() ? static_cast<size_t>(ringFrames_) * 2 * sizeof(float) * slots_.size() : 0;
}

void DiskStreamer::start()
{
    if (running_.load(std::memory_order_acquire) || config_.numThreads <= 0)
        return;
    stopRequested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    for (int i = 0; i < config_.numThreads; ++i)
        threads_.emplace_back([this] { workerLoop(); });
}

void DiskStreamer::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    for (auto& t : threads_)
        if (t.joinable())
            t.join();
    threads_.clear();
    running_.store(false, std::memory_order_release);
}

void DiskStreamer::workerLoop()
{
    while (!stopRequested_.load(std::memory_order_acquire))
    {
        bool worked = false;
        if (!paused_.load(std::memory_order_acquire))
        {
            // Drain everything that needs data, then nap. A ring holds >= ~40 ms even at the
            // fastest supported rate, so a 1 ms poll keeps far ahead of the voices.
            for (int i = 0; i < 64 && serviceOnce(); ++i)
                worked = true;
        }
        if (!worked)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int DiskStreamer::acquire(const std::shared_ptr<const SampleBuffer>& buffer, int64_t firstFrame) noexcept
{
    if (!buffer || !buffer->isStreaming() || !ringsReady_.load(std::memory_order_acquire))
        return -1;
    const size_t n = slots_.size();
    const size_t hint = acquireHint_.load(std::memory_order_relaxed) % n;
    for (size_t k = 0; k < n; ++k)
    {
        const size_t i = (hint + k) % n;
        Slot& s = slots_[i];
        int expected = Free;
        if (!s.state.compare_exchange_strong(expected, Claimed, std::memory_order_acquire))
            continue;
        s.buffer = buffer;                       // atomic refcount increment, no allocation
        s.channels.store(std::clamp(buffer->channels, 1, 2), std::memory_order_relaxed);
        s.length.store(buffer->length, std::memory_order_relaxed);
        const int64_t start = std::clamp<int64_t>(firstFrame, buffer->residentLength(), buffer->length);
        s.validFrom.store(start, std::memory_order_relaxed);
        s.writeEnd.store(start, std::memory_order_relaxed);
        s.readFloor.store(start, std::memory_order_relaxed);
        s.rate.store(1.0, std::memory_order_relaxed);
        s.failed.store(false, std::memory_order_relaxed);
        s.state.store(Active, std::memory_order_release);
        acquireHint_.store(static_cast<uint32_t>(i + 1), std::memory_order_relaxed);
        return static_cast<int>(i);
    }
    return -1;
}

void DiskStreamer::release(int slot) noexcept
{
    if (slot < 0 || static_cast<size_t>(slot) >= slots_.size())
        return;
    Slot& s = slots_[static_cast<size_t>(slot)];
    for (;;)
    {
        int st = s.state.load(std::memory_order_acquire);
        if (st == Active)
        {
            if (s.state.compare_exchange_weak(st, Claimed, std::memory_order_acquire))
            {
                // Refcount decrement only: the pool keeps every buffer alive until its GC runs.
                s.buffer.reset();
                s.state.store(Free, std::memory_order_release);
                return;
            }
        }
        else if (st == Busy)
        {
            if (s.state.compare_exchange_weak(st, BusyRetiring, std::memory_order_acq_rel))
                return; // the reader frees it when its fill completes
        }
        else
            return; // Free / Claimed / already retiring
    }
}

DiskStreamer::Window DiskStreamer::window(int slot) const noexcept
{
    Window w;
    if (slot < 0 || static_cast<size_t>(slot) >= slots_.size())
        return w;
    const Slot& s = slots_[static_cast<size_t>(slot)];
    w.end = s.writeEnd.load(std::memory_order_acquire);
    w.from = std::max(s.validFrom.load(std::memory_order_acquire), w.end - static_cast<int64_t>(ringFrames_));
    w.ring = s.ring;
    w.mask = mask_;
    w.channels = s.channels.load(std::memory_order_relaxed);
    if (w.ring == nullptr)
        w.end = w.from; // nothing readable
    return w;
}

void DiskStreamer::publishProgress(int slot, int64_t floorFrame, double framesPerSample) noexcept
{
    if (slot < 0 || static_cast<size_t>(slot) >= slots_.size())
        return;
    Slot& s = slots_[static_cast<size_t>(slot)];
    // Floors only move forward (voices never play backwards).
    int64_t cur = s.readFloor.load(std::memory_order_relaxed);
    while (floorFrame > cur && !s.readFloor.compare_exchange_weak(cur, floorFrame, std::memory_order_release)) {}
    s.rate.store(std::max(1.0e-3, framesPerSample), std::memory_order_relaxed);
}

bool DiskStreamer::needsFill(const Slot& s, double& urgency) const noexcept
{
    // Only atomics here: the slot may be released concurrently (its buffer is not ours to read).
    if (s.failed.load(std::memory_order_relaxed) || s.ring == nullptr)
        return false;
    const int64_t length = s.length.load(std::memory_order_relaxed);
    const int64_t we = s.writeEnd.load(std::memory_order_relaxed);
    const int64_t floor = s.readFloor.load(std::memory_order_acquire);
    if (we >= length)
        return false;
    if (floor > we)
    {
        urgency = -1.0; // consumer already ran dry: most urgent
        return true;
    }
    const int64_t space = floor + ringFrames_ - we;
    const int64_t want = std::min<int64_t>(config_.chunkFrames / 2, length - we);
    if (space < want)
        return false;
    urgency = static_cast<double>(we - floor) / std::max(1.0e-3, s.rate.load(std::memory_order_relaxed));
    return true;
}

bool DiskStreamer::serviceOnce() noexcept
{
    if (!ringsReady_.load(std::memory_order_acquire))
        return false;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        int best = -1;
        double bestUrgency = std::numeric_limits<double>::max();
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            const Slot& s = slots_[i];
            if (s.state.load(std::memory_order_acquire) != Active)
                continue;
            double u = 0.0;
            if (needsFill(s, u) && u < bestUrgency)
            {
                bestUrgency = u;
                best = static_cast<int>(i);
            }
        }
        if (best < 0)
            return false;
        Slot& s = slots_[static_cast<size_t>(best)];
        int expected = Active;
        if (!s.state.compare_exchange_strong(expected, Busy, std::memory_order_acquire))
            continue; // another reader took it, or it was released: rescan
        const int64_t n = fillSlot(s);
        endBusy(s);
        if (n > 0)
            return true;
    }
    return false;
}

int64_t DiskStreamer::fillSlot(Slot& s) noexcept
{
    if (!s.buffer || !s.buffer->stream || s.ring == nullptr)
        return 0;
    const int ch = s.channels.load(std::memory_order_relaxed);
    const int64_t length = s.length.load(std::memory_order_relaxed);
    int64_t floor = s.readFloor.load(std::memory_order_acquire);
    int64_t we = s.writeEnd.load(std::memory_order_relaxed);
    if (floor > we)
    {
        // Underrun recovery: skip ahead to where the voice is now.
        floor = std::min(floor, length);
        s.validFrom.store(floor, std::memory_order_relaxed);
        s.writeEnd.store(floor, std::memory_order_release);
        we = floor;
    }
    const int64_t space = floor + ringFrames_ - we;
    int64_t n = std::min<int64_t>({ static_cast<int64_t>(config_.chunkFrames), space, length - we });
    if (n <= 0)
        return 0;

    int64_t written = 0;
    while (written < n)
    {
        const int64_t pos = (we + written) & mask_;
        const int64_t part = std::min(n - written, static_cast<int64_t>(ringFrames_) - pos);
        const int64_t got = s.buffer->stream->readFrames(we + written, part, s.ring + pos * ch);
        written += std::max<int64_t>(0, got);
        if (got < part)
        {
            s.failed.store(true, std::memory_order_relaxed);
            readErrors_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    if (written > 0)
    {
        s.writeEnd.store(we + written, std::memory_order_release);
        framesStreamed_.fetch_add(static_cast<uint64_t>(written), std::memory_order_relaxed);
    }
    return written;
}

void DiskStreamer::endBusy(Slot& s) noexcept
{
    int expected = Busy;
    if (s.state.compare_exchange_strong(expected, Active, std::memory_order_release))
        return;
    // Released while we were reading (BusyRetiring): we own it now.
    s.buffer.reset();
    s.state.store(Free, std::memory_order_release);
}

bool DiskStreamer::fillBlocking(int slot, int64_t floorFrame, int64_t endFrame) noexcept
{
    if (slot < 0 || static_cast<size_t>(slot) >= slots_.size())
        return false;
    Slot& s = slots_[static_cast<size_t>(slot)];
    publishProgress(slot, floorFrame, s.rate.load(std::memory_order_relaxed));
    endFrame = std::min(endFrame, s.length.load(std::memory_order_relaxed));
    if (endFrame - std::max(floorFrame, s.validFrom.load(std::memory_order_relaxed)) > ringFrames_)
        return false;
    for (int guard = 0; guard < 1 << 20; ++guard)
    {
        if (s.writeEnd.load(std::memory_order_acquire) >= endFrame
            && s.validFrom.load(std::memory_order_acquire) <= floorFrame)
            return true;
        if (s.failed.load(std::memory_order_relaxed))
            return false;
        int st = s.state.load(std::memory_order_acquire);
        if (st == Active)
        {
            if (s.state.compare_exchange_strong(st, Busy, std::memory_order_acquire))
            {
                const int64_t n = fillSlot(s);
                endBusy(s);
                if (n <= 0 && s.writeEnd.load(std::memory_order_acquire) < endFrame)
                    return false;
            }
        }
        else if (st == Busy)
            std::this_thread::yield(); // a reader thread is filling it right now
        else
            return false;
    }
    return false;
}

int DiskStreamer::activeStreams() const noexcept
{
    int n = 0;
    for (const auto& s : slots_)
    {
        const int st = s.state.load(std::memory_order_relaxed);
        if (st == Active || st == Busy)
            ++n;
    }
    return n;
}

} // namespace looper

namespace looper {

int DiskStreamer::freeSlots() const noexcept
{
    int n = 0;
    for (const auto& s : slots_)
        if (s.state.load(std::memory_order_relaxed) == Free)
            ++n;
    return n;
}

} // namespace looper
