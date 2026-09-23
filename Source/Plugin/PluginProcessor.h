#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Import/ImportController.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "../MidiRouter/MidiRouter.h"
#include "../SamplePool/SamplePool.h"
#include "../PatchStore/PatchStore.h"
#include "../Prefs/SessionPrefs.h"
#include "../VoiceEngine/VoiceEngine.h"

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

class LooperAudioProcessor : public juce::AudioProcessor
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

    /** Message-thread: decode + AutoMapper → pending review. */
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

    juce::AudioProcessorValueTreeState apvts_;
    SamplePool samplePool_;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperAudioProcessor)
};
