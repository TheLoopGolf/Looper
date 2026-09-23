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

    void storeLastReview(AutoMapResult result, std::vector<SampleRef> refs)
    {
        lastReview_ = std::move(result);
        lastReviewRefs_ = std::move(refs);
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
        }
    }

    static std::string makeSampleId(const juce::File& file);

private:
    juce::AudioFormatManager formatManager_;
    bool formatsReady_ = false;
    std::optional<AutoMapResult> pending_;
    std::vector<SampleRef> pendingRefs_;
    std::optional<AutoMapResult> lastReview_;
    std::vector<SampleRef> lastReviewRefs_;
};

} // namespace looper
