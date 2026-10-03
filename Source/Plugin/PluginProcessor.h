#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Import/ImportController.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "../MidiRouter/MidiRouter.h"
#include "../SamplePool/SamplePool.h"
#include "../PatchStore/PatchStore.h"
#include "../PatchStore/SampleRelocator.h"
#include "../Prefs/SessionPrefs.h"
#include "../VoiceEngine/VoiceEngine.h"
#include "../ZoneEdit/EditHistory.h"
#include "../ZoneEdit/ZoneEditor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

using looper::ImportController;
using looper::InstrumentMap;
using looper::MidiRouter;
using looper::SamplePool;
using looper::SampleRef;
using looper::Patch;
using looper::PatchLoadResult;
using looper::PatchStore;
using looper::SessionPrefs;
using looper::VoiceEngine;

class LooperAudioProcessor : public juce::AudioProcessor,
                             public looper::ZoneEditTarget
{
public:
    LooperAudioProcessor();
    ~LooperAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    SamplePool& samplePool() { return samplePool_; }
    ImportController& importController() { return importController_; }

    InstrumentMap copyInstrumentMap() const;
    bool hasUserInstrument() const { return hasUserInstrument_.load(); }
    int zoneCount() const;
    int rootCount() const;
    int rrDepth() const;
    /** Message-thread / UI: key spans for zone keyboard visualization. */
    std::vector<looper::ZoneKeySpan> getZoneKeySpans() const;
    const std::vector<SampleRef>& userSampleRefs() const { return userSampleRefs_; }

    /** Message-thread: decode + AutoMapper -> pending review. */
    bool importAudioFiles(const juce::Array<juce::File>& filesOrFolders);
    bool acceptPendingMap();
    void discardPendingMap();
    bool reopenLastReview();

    /** Build a Patch from current map + user samples + APVTS snapshot. */
    Patch buildCurrentPatch() const;

    /** Message-thread: write .looper.json / .json; remember last path/name. */
    bool savePatchToFile(const juce::File& file);

    /**
     * Message-thread: parse patch, resolve paths, decode available samples into pool,
     * adopt map. Returns false on hard parse failure. Offline sample ids reported via out.
     */
    bool loadPatchFromFile(const juce::File& file, juce::StringArray* missingPathsOut = nullptr);

    /** Apply an already-parsed patch (paths should be absolute or resolvable). */
    bool applyPatch(const Patch& patch, juce::StringArray* missingPathsOut = nullptr);

    const juce::String& lastPatchPath() const { return lastPatchPath_; }
    const juce::String& patchName() const { return patchName_; }
    void setPatchName(const juce::String& name) { patchName_ = name; }

    const std::vector<std::string>& offlineSampleIds() const { return offlineSampleIds_; }
    bool hasMissingSamples() const { return ! offlineSampleIds_.empty(); }

    /** Message-thread: offline samples (id + last known absolute path) for the Relocate screen. */
    std::vector<looper::MissingSample> missingSamples() const;

    /**
     * Message-thread: point sample `sampleId` at `newFile`, decode it into the pool
     * (zones referencing it become audible immediately) and mark the patch dirty so
     * the next save writes the new relative path. Returns false (with error) if the
     * file could not be decoded; the sample then stays offline.
     */
    bool relocateSample(const std::string& sampleId, const juce::File& newFile, juce::String* error = nullptr);

    /**
     * True when the in-memory instrument differs from the last saved/loaded patch file.
     * Follows undo/redo: undoing back to the saved state reads as clean again.
     */
    bool isPatchDirty() const { return editState_.isDirty(); }
    /** Message thread: saved-state tracking (tests / diagnostics). */
    const looper::EditStateTracker& editState() const { return editState_; }

    /** Round-robin mode from the host-automatable "rrMode" parameter (Cycle / Random). */
    looper::RoundRobinMode currentRoundRobinMode() const;
    /** Message thread: set "rrMode" (notifies host; saved with the patch). */
    void setRoundRobinMode(looper::RoundRobinMode mode);

    // --- Manual zone editor (main screen) ----------------------------------------------
    /** Undo history for zone edits (cleared when an import is accepted or a patch is loaded). */
    juce::UndoManager& undoManager() { return undoManager_; }

    /**
     * Message thread: make zones[index] equal `proposed` (sanitized) as an undoable action.
     * newTransaction = false merges into the current transaction (slider drag in progress).
     * Applies live to playback, marks the patch unsaved. False if nothing changed / stale index.
     */
    bool performZoneEdit(size_t index, const looper::Zone& proposed, const juce::String& actionName,
                         bool newTransaction = true);

    /**
     * Message thread: apply a group of proposed zones (multi-selection edit, strip drag) as ONE
     * undoable action, published to playback as one live map. Unchanged zones are skipped;
     * a stale index / sample id rejects the whole group. False if nothing changed.
     */
    bool performZoneEdits(const std::vector<looper::ZoneChange>& changes, const juce::String& actionName,
                          bool newTransaction = true);

    /** ZoneEditTarget: swap the edited zone into the live map (used by undo/redo too). */
    bool replaceZone(size_t index, const std::string& sampleId, const looper::Zone& zone) override;
    /** ZoneEditTarget: apply / revert a group of edits with a single live map swap. */
    bool replaceZones(const std::vector<looper::ZoneEdit>& edits, bool forward) override;

    /** Message thread: sample metadata for a zone's sample id (nullptr if unknown). */
    const SampleRef* findSampleRef(const std::string& sampleId) const;

    /**
     * Message thread: hold (down = true) / release an audition of exactly zones[zoneIndex]
     * (its root key, clamped into its key range, velocity 100 clamped into its layer). Plays that
     * zone even when other zones overlap it or it belongs to a round-robin group.
     */
    void auditionZone(int zoneIndex, bool down);

    // --- Disk streaming / sample memory ------------------------------------------------------
    /** Per-patch "Load fully into RAM" (saved in the patch and host session). */
    bool loadIntoRam() const { return loadIntoRam_; }
    /**
     * Message thread: switch between streaming and fully-in-RAM for this patch. Re-decodes the
     * patch's samples in the new mode (sounding notes finish on their old buffers) and marks the
     * patch unsaved. No-op if unchanged.
     */
    void setLoadIntoRam(bool fully);

    struct MemoryStatus
    {
        size_t ramBytes = 0;          // resident sample audio + stream rings
        size_t sampleBytes = 0;       // resident sample audio only
        size_t fullBytes = 0;         // the same samples fully decoded
        size_t ringBytes = 0;
        int samples = 0;
        int streamingSamples = 0;     // samples whose tail streams from disk
        int activeStreams = 0;        // voices streaming right now
        uint64_t underruns = 0;       // dropouts since load (or resetUnderruns)
        bool loadIntoRam = false;
        int preloadFrames = 0;
    };
    /** Message thread (UI chip / Settings). Cheap: one pass over the pool under its lock. */
    MemoryStatus memoryStatus() const;
    void resetUnderruns() { voiceEngine_.resetUnderruns(); }

    looper::DiskStreamer& diskStreamer() { return diskStreamer_; }
    VoiceEngine& voiceEngine() { return voiceEngine_; }

    const SessionPrefs& sessionPrefs() const { return prefs_; }
    /** Apply prefs to engine / map / APVTS defaults and remember for persistence. */
    void applySessionPrefs(const SessionPrefs& prefs);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void ensureDemoInstrument();
    void syncParamsToEngine();
    void swapPlayableMap(InstrumentMap newMap);
    void pushPrefsOntoMap(InstrumentMap& map) const;
    void applyPrefsToRuntime();
    looper::StreamingOptions currentStreamingOptions() const;
    /** Re-decode every loaded user sample with the current streaming options. */
    void reloadSamplesForStreaming();
    /** Allocate stream rings once any loaded sample actually streams. */
    void ensureStreamingReady();

    juce::AudioProcessorValueTreeState apvts_;
    // Declaration order matters: the pool and streamer must outlive the voice engine.
    SamplePool samplePool_;
    looper::DiskStreamer diskStreamer_;
    bool loadIntoRam_ = false;
    /** Message-thread housekeeping: frees replaced buffers / old pool snapshots. */
    struct PoolGcTimer : juce::Timer
    {
        SamplePool* pool = nullptr;
        void timerCallback() override { if (pool) pool->collectGarbage(); }
    } poolGc_;
    ImportController importController_;
    SessionPrefs prefs_;

    mutable std::mutex mapMutex_;
    InstrumentMap map_;
    std::shared_ptr<InstrumentMap> mapShared_;

    VoiceEngine voiceEngine_;
    MidiRouter midiRouter_;
    bool demoLoaded_ = false;
    std::atomic<bool> hasUserInstrument_ { false };
    std::vector<SampleRef> userSampleRefs_;
    juce::String lastPatchPath_;
    juce::String patchName_ { "Untitled" };
    std::vector<std::string> offlineSampleIds_;
    looper::EditStateTracker editState_;   // "unsaved" marker; follows undo/redo

    juce::UndoManager undoManager_ { 0, 200 };  // zone edits only; ~200 transactions kept
    bool auditioning_ = false;                  // a VoiceEngine audition request is held

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperAudioProcessor)
};
