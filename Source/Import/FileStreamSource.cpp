#include "FileStreamSource.h"

#include <algorithm>
#include <list>

namespace looper {

// --- Process-wide open-reader LRU ------------------------------------------------------------
struct ReaderRegistry
{
    std::mutex mutex;
    std::list<FileStreamSource*> lru; // front = most recently used, all with an open reader
    int maxOpen = 48;

    static ReaderRegistry& get()
    {
        static ReaderRegistry r;
        return r;
    }

    /** Called with `src`'s mutex held, after it opened (or used) its reader. */
    void touch(FileStreamSource* src)
    {
        std::lock_guard<std::mutex> lock(mutex);
        lru.remove(src);
        lru.push_front(src);
        // Evict least recently used readers that are not mid-read (try-lock never deadlocks).
        auto it = lru.end();
        while ((int) lru.size() > maxOpen && it != lru.begin())
        {
            --it;
            if (*it == src)
                continue;
            if ((*it)->tryCloseReader())
                it = lru.erase(it);
        }
    }

    void remove(FileStreamSource* src)
    {
        std::lock_guard<std::mutex> lock(mutex);
        lru.remove(src);
    }

    int count()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return (int) lru.size();
    }
};

namespace {
juce::AudioFormatManager& sharedFormats(std::mutex*& guardOut)
{
    static std::mutex guard;
    static juce::AudioFormatManager fm;
    static std::once_flag once;
    std::call_once(once, [] { fm.registerBasicFormats(); });
    guardOut = &guard;
    return fm;
}
} // namespace

FileStreamSource::FileStreamSource(const juce::File& file, int outChannels, int64_t lengthFrames)
    : file_(file), outChannels_(std::clamp(outChannels, 1, 2)), length_(lengthFrames)
{
}

FileStreamSource::~FileStreamSource()
{
    ReaderRegistry::get().remove(this);
    std::lock_guard<std::mutex> lock(mutex_);
    reader_.reset();
}

int FileStreamSource::openReaderCount() { return ReaderRegistry::get().count(); }

void FileStreamSource::setMaxOpenReaders(int n)
{
    auto& r = ReaderRegistry::get();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.maxOpen = std::max(1, n);
}

bool FileStreamSource::tryCloseReader()
{
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    reader_.reset();
    return true;
}

void FileStreamSource::downmixToStereo(juce::AudioBuffer<float>& buffer)
{
    const int ch = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (ch <= 2 || n <= 0) return;
    // Plain per-frame loop (not SIMD helpers): identical arithmetic for every frame, so a
    // streamed chunk downmixes to exactly the floats of the whole-file decode.
    juce::AudioBuffer<float> stereo(2, n);
    const float scale = 1.0f / (float) ch;
    for (int side = 0; side < 2; ++side)
    {
        float* dst = stereo.getWritePointer(side);
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            for (int c = side; c < ch; c += 2)
                acc += buffer.getReadPointer(c)[i] * scale;
            dst[i] = acc;
        }
    }
    buffer = std::move(stereo);
}

void FileStreamSource::interleave(const juce::AudioBuffer<float>& planar, int outChannels, int numFrames, float* out)
{
    const int inCh = planar.getNumChannels();
    for (int i = 0; i < numFrames; ++i)
        for (int c = 0; c < outChannels; ++c)
            out[(size_t) i * (size_t) outChannels + (size_t) c] = planar.getReadPointer(std::min(c, inCh - 1))[i];
}

int64_t FileStreamSource::readFrames(int64_t start, int64_t frames, float* out) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (start < 0 || frames <= 0 || start >= length_)
            return 0;
        frames = std::min(frames, length_ - start);
        if (!reader_)
        {
            std::mutex* guard = nullptr;
            auto& fm = sharedFormats(guard);
            std::lock_guard<std::mutex> g(*guard);
            reader_.reset(fm.createReaderFor(file_));
            if (!reader_)
                return 0;
        }
        ReaderRegistry::get().touch(this);

        constexpr int64_t kPiece = 16384;
        int64_t done = 0;
        while (done < frames)
        {
            const int n = (int) std::min(kPiece, frames - done);
            const int inCh = std::max(1, (int) reader_->numChannels);
            // Keep the scratch buffer's channel count; avoid reallocating per read.
            scratch_.setSize(inCh, n, false, false, true);
            if (!reader_->read(&scratch_, 0, n, start + done, true, true))
                break;
            if (inCh > 2)
            {
                juce::AudioBuffer<float> mix(scratch_.getArrayOfWritePointers(), inCh, n);
                downmixToStereo(mix);
                interleave(mix, outChannels_, n, out + done * outChannels_);
            }
            else
                interleave(scratch_, outChannels_, n, out + done * outChannels_);
            done += n;
        }
        return done;
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace looper
