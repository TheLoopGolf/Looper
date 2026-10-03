#pragma once

#include "../AutoMapper/AutoMapper.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "../SamplePool/SamplePool.h"
#include "MapCommit.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace looper {

/** How loadFileIntoPool keeps decoded audio (Settings: preload size / "Load fully into RAM"). */
struct StreamingOptions
{
    /** Frames kept in RAM per sample; longer samples stream the remainder from disk. */
    int64_t preloadFrames = 65536;
    /** Decode every sample completely (small patches, offline bounce). */
    bool loadFully = false;

    bool operator==(const StreamingOptions&) const = default;
};

struct LoadedSample
{
    SampleRef ref;
    bool ok = false;
    juce::String error;
};

/** Message-thread import pipeline. Never call from the audio thread. */
class ImportController
{
public:
    ImportController();

    void prepareFormats();

    static bool isSupportedAudioExtension(const juce::String& ext);
    static juce::Array<juce::File> collectAudioFiles(const juce::StringArray& paths);
    static juce::Array<juce::File> collectAudioFiles(const juce::Array<juce::File>& filesOrFolders);

    LoadedSample loadFileIntoPool(const juce::File& file, SamplePool& pool);

    /** Applies to every later load (import, patch load, relocate). Message thread. */
    void setStreamingOptions(StreamingOptions o) { streaming_ = o; }
    const StreamingOptions& streamingOptions() const { return streaming_; }

    /**
     * Decode `file` as SampleBuffer: fully, or (streaming) only the first preload frames plus a
     * FileStreamSource for the rest. Returns false + error when the file cannot be read.
     */
    bool decodeFile(const juce::File& file, SampleBuffer& out, juce::String& error);

    /** Load using an existing SampleRef id/metadata (patch reload). path taken from file. */
    LoadedSample loadFileIntoPool(const juce::File& file, SamplePool& pool, const SampleRef& preserve);

    bool importFiles(const juce::Array<juce::File>& files,
                     SamplePool& pool,
                     const std::vector<SampleRef>& existingUserRefs = {},
                     AutoMapOptions options = {});

    bool hasPending() const { return pending_.has_value(); }
    const AutoMapResult* pendingResult() const { return pending_ ? &*pending_ : nullptr; }
    const std::vector<SampleRef>& pendingSampleRefs() const { return pendingRefs_; }

    void clearPending()
    {
        pending_.reset();
        pendingRefs_.clear();
    }

    /**
     * "Use detected" from the Review screen: re-map the pending import with this sample's
     * confident detection replacing its filename note. Returns false if not applicable.
     */
    bool useDetectedPitch(const std::string& sampleId);

    void storeLastReview(AutoMapResult result, std::vector<SampleRef> refs)
    {
        lastReview_ = std::move(result);
        lastReviewRefs_ = std::move(refs);
        lastOptions_ = pendingOptions_;
        lastAnalyses_ = pendingAnalyses_;
    }

    bool hasLastReview() const { return lastReview_.has_value(); }
    const AutoMapResult* lastReview() const { return lastReview_ ? &*lastReview_ : nullptr; }
    const std::vector<SampleRef>& lastReviewRefs() const { return lastReviewRefs_; }

    void reopenLastAsPending()
    {
        if (lastReview_)
        {
            pending_ = *lastReview_;
            pendingRefs_ = lastReviewRefs_;
            pendingOptions_ = lastOptions_;
            pendingAnalyses_ = lastAnalyses_;
        }
    }

    static std::string makeSampleId(const juce::File& file);

    /**
     * YIN pitch analysis of a decoded buffer (JUCE-free core, see PitchDetector).
     * Cached per sample id; the cache entry is dropped when the file is reloaded.
     */
    PitchAnalysis analysePitch(const std::string& sampleId, const SampleBuffer& buffer);

private:
    juce::AudioFormatManager formatManager_;
    bool formatsReady_ = false;
    std::optional<AutoMapResult> pending_;
    std::vector<SampleRef> pendingRefs_;
    std::optional<AutoMapResult> lastReview_;
    std::vector<SampleRef> lastReviewRefs_;
    AutoMapOptions pendingOptions_, lastOptions_;
    PitchAnalysisMap pendingAnalyses_, lastAnalyses_;
    PitchAnalysisMap pitchCache_;
    StreamingOptions streaming_;
};

} // namespace looper
