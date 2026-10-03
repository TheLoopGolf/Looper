#pragma once

#include "../SamplePool/SampleStream.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <mutex>

namespace looper {

/**
 * Streams a sample's frames from its audio file (WAV / AIFF / FLAC via JUCE readers).
 *
 * Produces exactly the floats the full decode in ImportController produces (same reader, same
 * >2-channel downmix, same interleave), so streamed playback is bit-identical to fully loaded
 * playback. Thread-safe: a per-source mutex serialises reads of its reader. Readers are opened
 * lazily and a process-wide LRU caps how many stay open (big libraries would otherwise exhaust
 * file handles; macOS defaults to 256).
 */
class FileStreamSource : public SampleStreamSource
{
public:
    /** `outChannels` = channels of the decoded SampleBuffer (1 or 2). */
    FileStreamSource(const juce::File& file, int outChannels, int64_t lengthFrames);
    ~FileStreamSource() override;

    int64_t readFrames(int64_t start, int64_t frames, float* out) noexcept override;

    /** Readers currently open across all sources (tests / diagnostics). */
    static int openReaderCount();
    /** Cap on simultaneously open readers (default 48). */
    static void setMaxOpenReaders(int n);

    /**
     * Shared decode helpers so the full-load and streaming paths stay identical: downmix > 2
     * channels to stereo, then interleave `planar` (1-2 channels) into `out`.
     */
    static void downmixToStereo(juce::AudioBuffer<float>& buffer);
    static void interleave(const juce::AudioBuffer<float>& planar, int outChannels, int numFrames, float* out);

private:
    friend struct ReaderRegistry;
    /** Registry eviction: close the reader if no read is in progress. */
    bool tryCloseReader();

    juce::File file_;
    int outChannels_ = 1;
    int64_t length_ = 0;
    std::mutex mutex_;
    std::unique_ptr<juce::AudioFormatReader> reader_;
    juce::AudioBuffer<float> scratch_;
};

} // namespace looper
