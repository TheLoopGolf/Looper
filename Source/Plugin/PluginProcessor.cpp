#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Import/MapCommit.h"
#include "../ZoneEdit/ZoneEditAction.h"

#include <algorithm>

namespace {
float dbToLin(float db) { return juce::Decibels::decibelsToGain(db); }

looper::FilterType filterTypeFromChoice(int index)
{
    switch (index)
    {
        case 1:  return looper::FilterType::HighPass;
        case 2:  return looper::FilterType::BandPass;
        default: return looper::FilterType::LowPass;
    }
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LooperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack",
        juce::NormalisableRange<float>(0.1f, 5000.0f, 0.01f, 0.35f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"decay", 1}, "Decay",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.01f, 0.35f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sustain", 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1}, "Release",
        juce::NormalisableRange<float>(1.0f, 8000.0f, 0.01f, 0.35f), 200.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"volume", 1}, "Volume",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filterType", 1}, "Filter Type",
        juce::StringArray{"Low Pass", "High Pass", "Band Pass"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cutoff", 1}, "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.3f), 12000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"resonance", 1}, "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterEnvAmt", 1}, "Filter Env",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    // Round-robin mode (per patch; mirrored into .looper.json map.roundRobinMode).
    // Index order must match looper::RoundRobinMode (0 = Cycle default, 1 = Random).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"rrMode", 1}, "Round Robin",
        juce::StringArray{"Cycle", "Random"}, 0));
    return { params.begin(), params.end() };
}

LooperAudioProcessor::LooperAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", createParameterLayout())
{
    midiRouter_.setEngine(&voiceEngine_);
    mapShared_ = std::make_shared<InstrumentMap>();
    map_ = *mapShared_;
    voiceEngine_.adoptMap(mapShared_);
    voiceEngine_.setSamplePool(&samplePool_);
    voiceEngine_.setStreamer(&diskStreamer_);
    diskStreamer_.start();
    importController_.setStreamingOptions(currentStreamingOptions());
    poolGc_.pool = &samplePool_;
    poolGc_.startTimer(1000);
    // Random round-robin: fresh sequence per plugin instance (tests seed explicitly).
    voiceEngine_.setRandomSeed(static_cast<uint64_t>(juce::Time::getHighResolutionTicks())
                               ^ static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64()));
    applyPrefsToRuntime();
    ensureDemoInstrument();
}

LooperAudioProcessor::~LooperAudioProcessor()
{
    poolGc_.stopTimer();
    diskStreamer_.stop();
}

looper::StreamingOptions LooperAudioProcessor::currentStreamingOptions() const
{
    looper::StreamingOptions o;
    o.preloadFrames = juce::jlimit(SessionPrefs::kMinPreloadFrames, SessionPrefs::kMaxPreloadFrames, prefs_.preloadFrames);
    o.loadFully = loadIntoRam_;
    return o;
}

void LooperAudioProcessor::ensureStreamingReady()
{
    if (samplePool_.memoryStats().streamingSamples > 0)
        diskStreamer_.ensureRings();
}

void LooperAudioProcessor::reloadSamplesForStreaming()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto opts = currentStreamingOptions();
    if (opts == importController_.streamingOptions())
        return;
    importController_.setStreamingOptions(opts);
    {
        SamplePool::ScopedBatch batch(samplePool_);
        for (auto& ref : userSampleRefs_)
        {
            if (std::find(offlineSampleIds_.begin(), offlineSampleIds_.end(), ref.id) != offlineSampleIds_.end())
                continue;
            if (ref.path.find("://") != std::string::npos)
                continue;
            const juce::File f(ref.path);
            if (! f.existsAsFile())
                continue;
            // Sounding voices keep their old buffer (and stream slot) until they end.
            auto loaded = importController_.loadFileIntoPool(f, samplePool_, ref);
            if (loaded.ok)
                ref = loaded.ref;
        }
        ensureStreamingReady(); // before the batch publishes any streamed buffer
    }
    samplePool_.collectGarbage();
}

void LooperAudioProcessor::setLoadIntoRam(bool fully)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (fully == loadIntoRam_)
        return;
    loadIntoRam_ = fully;
    {
        auto next = copyInstrumentMap();
        next.loadIntoRam = fully;
        swapPlayableMap(std::move(next));
    }
    reloadSamplesForStreaming();
    if (hasUserInstrument_.load())
        editState_.markExternalChange(); // saved with the patch: unsaved until the next save
}

LooperAudioProcessor::MemoryStatus LooperAudioProcessor::memoryStatus() const
{
    MemoryStatus st;
    const auto pool = samplePool_.memoryStats();
    st.sampleBytes = pool.residentBytes;
    st.fullBytes = pool.fullBytes;
    st.samples = pool.samples;
    st.streamingSamples = pool.streamingSamples;
    // Stream rings are only counted while something actually streams, so the
    // "In RAM" figure matches the decoded sample data the user asked for.
    st.ringBytes = st.streamingSamples > 0 ? diskStreamer_.ringBytes() : 0;
    st.ramBytes = st.sampleBytes + st.ringBytes;
    st.activeStreams = voiceEngine_.streamingStats().streamingVoices;
    st.underruns = voiceEngine_.underrunCount();
    st.loadIntoRam = loadIntoRam_;
    st.preloadFrames = (int) currentStreamingOptions().preloadFrames;
    return st;
}

void LooperAudioProcessor::ensureDemoInstrument()
{
    if (demoLoaded_ || hasUserInstrument_.load())
        return;
    samplePool_.loadDemoSample(44100.0);
    InstrumentMap demo;
    demo.zones.push_back(looper::makeDemoZone());
    pushPrefsOntoMap(demo);
    swapPlayableMap(std::move(demo));
    demoLoaded_ = true;
}

void LooperAudioProcessor::swapPlayableMap(InstrumentMap newMap)
{
    auto next = std::make_shared<InstrumentMap>(std::move(newMap));
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        mapShared_ = next;
        map_ = *next;
    }
    voiceEngine_.adoptMap(next);
    voiceEngine_.setPolyphony(next->polyphonyLimit > 0 ? next->polyphonyLimit : 64);
}

InstrumentMap LooperAudioProcessor::copyInstrumentMap() const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    return map_;
}

int LooperAudioProcessor::zoneCount() const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    return (int) map_.zones.size();
}

std::vector<looper::ZoneKeySpan> LooperAudioProcessor::getZoneKeySpans() const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    std::vector<looper::ZoneKeySpan> out;
    out.reserve(map_.zones.size());
    for (const auto& z : map_.zones)
        out.push_back({ z.keyLow, z.keyHigh, z.rootKey });
    return out;
}

int LooperAudioProcessor::rootCount() const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    return looper::countUniqueRoots(map_);
}

int LooperAudioProcessor::rrDepth() const
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    return looper::maxRrDepth(map_);
}

bool LooperAudioProcessor::importAudioFiles(const juce::Array<juce::File>& filesOrFolders)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    importController_.setStreamingOptions(currentStreamingOptions());
    const bool ok = importController_.importFiles(filesOrFolders, samplePool_, userSampleRefs_,
                                                  prefs_.toAutoMapOptions())
                    && importController_.hasPending();
    ensureStreamingReady();
    return ok;
}

bool LooperAudioProcessor::acceptPendingMap()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto* pending = importController_.pendingResult();
    if (!pending) return false;
    auto next = copyInstrumentMap();
    looper::commitAutoMapToInstrument(next, *pending);
    pushPrefsOntoMap(next);
    userSampleRefs_ = importController_.pendingSampleRefs();
    importController_.storeLastReview(*pending, userSampleRefs_);
    importController_.clearPending();
    swapPlayableMap(std::move(next));
    undoManager_.clearUndoHistory(); // zone indices of the old map are meaningless now
    editState_.reset(true);          // a fresh import is unsaved until written
    hasUserInstrument_.store(true);
    demoLoaded_ = false;
    if (patchName_.isEmpty() || patchName_ == "Untitled")
    {
        if (!userSampleRefs_.empty())
        {
            const auto& dn = userSampleRefs_.front().displayName;
            auto base = juce::File(juce::String(dn)).getFileNameWithoutExtension();
            if (base.isNotEmpty())
                patchName_ = base;
        }
    }
    offlineSampleIds_.clear();
    return true;
}

void LooperAudioProcessor::discardPendingMap()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    importController_.clearPending();
}

bool LooperAudioProcessor::reopenLastReview()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!importController_.hasLastReview()) return false;
    importController_.reopenLastAsPending();
    return importController_.hasPending();
}

void LooperAudioProcessor::pushPrefsOntoMap(InstrumentMap& map) const
{
    map.polyphonyLimit = prefs_.polyphony;
    map.glideMs = prefs_.glideMs;
    map.velCurve = prefs_.velCurve;
    map.modWheelTarget = prefs_.modWheelTarget;
    map.loadIntoRam = loadIntoRam_;
}

void LooperAudioProcessor::applyPrefsToRuntime()
{
    voiceEngine_.setPolyphony(prefs_.polyphony);
    voiceEngine_.setMasterSoftClip(prefs_.masterSoftClip);
    // Glide: stored on InstrumentMap.glideMs; VoiceEngine applies legato portamento when > 0.
    midiRouter_.setBendRangeSemis(prefs_.pitchBendRangeSemis);
    midiRouter_.setModWheelTarget(prefs_.modWheelTarget);

    if (auto* param = apvts_.getParameter("filterType"))
    {
        const float denorm = static_cast<float>(prefs_.defaultFilterType);
        param->setValueNotifyingHost(param->convertTo0to1(denorm));
    }

    // Push map-global fields onto the live map when present
    auto next = copyInstrumentMap();
    pushPrefsOntoMap(next);
    swapPlayableMap(std::move(next));
}

void LooperAudioProcessor::applySessionPrefs(const SessionPrefs& prefs)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    prefs_ = prefs;
    prefs_.polyphony = juce::jlimit(1, 128, prefs_.polyphony);
    prefs_.glideMs = juce::jmax(0.0f, prefs_.glideMs);
    prefs_.preloadFrames = juce::jlimit(SessionPrefs::kMinPreloadFrames, SessionPrefs::kMaxPreloadFrames,
                                        prefs_.preloadFrames);
    applyPrefsToRuntime();
    syncParamsToEngine();
    reloadSamplesForStreaming(); // no-op unless the preload size changed
}

void LooperAudioProcessor::syncParamsToEngine()
{
    looper::AmpEnv::Params env;
    env.attackMs = apvts_.getRawParameterValue("attack")->load();
    env.decayMs = apvts_.getRawParameterValue("decay")->load();
    env.sustain = apvts_.getRawParameterValue("sustain")->load();
    env.releaseMs = apvts_.getRawParameterValue("release")->load();
    voiceEngine_.setEnvParams(env);
    voiceEngine_.setMasterGainLin(dbToLin(apvts_.getRawParameterValue("volume")->load()));

    looper::FilterParams fp;
    fp.type = filterTypeFromChoice((int) apvts_.getRawParameterValue("filterType")->load());
    fp.cutoffHz = apvts_.getRawParameterValue("cutoff")->load();
    fp.resonance = apvts_.getRawParameterValue("resonance")->load();
    fp.envAmount = apvts_.getRawParameterValue("filterEnvAmt")->load();
    voiceEngine_.setFilterParams(fp);
    voiceEngine_.setRoundRobinMode(currentRoundRobinMode());

    looper::ModWheelTarget target = looper::ModWheelTarget::FilterCutoff;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        target = map_.modWheelTarget;
    }
    midiRouter_.setModWheelTarget(target);
}

void LooperAudioProcessor::prepareToPlay(double sampleRate, int)
{
    if (!hasUserInstrument_.load())
    {
        ensureDemoInstrument();
        if (auto existing = samplePool_.getBuffer(looper::kDemoSampleId))
        {
            if (std::abs(existing->sampleRate - sampleRate) > 1.0)
                samplePool_.loadDemoSample(sampleRate);
        }
        else samplePool_.loadDemoSample(sampleRate);
    }
    voiceEngine_.setSamplePool(&samplePool_);
    diskStreamer_.start();
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        if (mapShared_) voiceEngine_.adoptMap(mapShared_);
        voiceEngine_.setPolyphony(map_.polyphonyLimit > 0 ? map_.polyphonyLimit : 64);
    }
    voiceEngine_.setSampleRate(sampleRate);
    syncParamsToEngine();
}

void LooperAudioProcessor::releaseResources() {}

bool LooperAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled()) return false;
    return true;
}

void LooperAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    syncParamsToEngine();
    voiceEngine_.applyPendingMapUpdate();  // live zone edits, before this block's note-ons
    voiceEngine_.applyAuditionRequest();   // zone-editor audition (exact zone, bypasses RR/overlap)
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            midiRouter_.handleNoteOn(msg.getNoteNumber(), msg.getVelocity(), msg.getChannel());
        else if (msg.isNoteOff())
            midiRouter_.handleNoteOff(msg.getNoteNumber(), msg.getChannel());
        else if (msg.isPitchWheel())
            midiRouter_.handlePitchBend(msg.getPitchWheelValue(), msg.getChannel());
        else if (msg.isController())
            midiRouter_.handleCc(msg.getControllerNumber(), msg.getControllerValue(), msg.getChannel());
    }
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
    // Offline bounce: streamed frames are read on this thread when not yet buffered (no underruns).
    voiceEngine_.setNonRealtime(isNonRealtime());
    voiceEngine_.processBlock(left, right, buffer.getNumSamples());
}

juce::AudioProcessorEditor* LooperAudioProcessor::createEditor()
{
    return new LooperAudioProcessorEditor(*this);
}

Patch LooperAudioProcessor::buildCurrentPatch() const
{
    Patch patch;
    patch.schemaVersion = PatchStore::kSchemaVersion;
    patch.name = patchName_.toStdString();
    patch.patchRoot = ".";
    patch.map = copyInstrumentMap();
    patch.map.rrMode = currentRoundRobinMode(); // APVTS "rrMode" is the live source of truth
    patch.samples = userSampleRefs_;

    // Snapshot host-automatable floats for convenience (restored on load when present).
    static const char* kIds[] = {
        "attack", "decay", "sustain", "release", "volume",
        "filterType", "cutoff", "resonance", "filterEnvAmt"
    };
    for (auto* id : kIds)
        if (auto* raw = apvts_.getRawParameterValue(id))
            patch.params[id] = static_cast<double>(raw->load());
    return patch;
}

bool LooperAudioProcessor::savePatchToFile(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (file.getFullPathName().isEmpty())
        return false;

    auto patch = buildCurrentPatch();
    if (patch.name.empty() || patch.name == "Untitled")
        patch.name = file.getFileNameWithoutExtension().toStdString();

    const bool ok = PatchStore::save(file.getFullPathName().toStdString(), patch);
    if (ok)
    {
        lastPatchPath_ = file.getFullPathName();
        patchName_ = juce::String(patch.name);
        editState_.markSaved();
    }
    return ok;
}

bool LooperAudioProcessor::applyPatch(const Patch& patch, juce::StringArray* missingPathsOut)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    offlineSampleIds_.clear();
    std::vector<SampleRef> loadedRefs;
    loadedRefs.reserve(patch.samples.size());

    // Streaming mode is part of the patch: decode in the patch's mode from the start.
    loadIntoRam_ = patch.map.loadIntoRam;
    importController_.setStreamingOptions(currentStreamingOptions());
    samplePool_.beginBatch(); // publish all decoded samples at once, before the map swap

    for (const auto& refIn : patch.samples)
    {
        SampleRef ref = refIn;
        if (ref.path.find("://") != std::string::npos)
        {
            // Special / demo URI - keep metadata, no file load
            samplePool_.addStub(ref);
            loadedRefs.push_back(ref);
            continue;
        }

        const juce::File f(ref.path);
        if (!f.existsAsFile())
        {
            offlineSampleIds_.push_back(ref.id);
            if (missingPathsOut != nullptr)
                missingPathsOut->add(juce::String(ref.path));
            samplePool_.addStub(ref); // keep zone metadata; buffer absent -> silent/offline
            loadedRefs.push_back(ref);
            continue;
        }

        auto loaded = importController_.loadFileIntoPool(f, samplePool_, ref);
        if (!loaded.ok)
        {
            offlineSampleIds_.push_back(ref.id);
            if (missingPathsOut != nullptr)
                missingPathsOut->add(juce::String(ref.path));
            samplePool_.addStub(ref);
            loadedRefs.push_back(ref);
            continue;
        }
        loadedRefs.push_back(loaded.ref);
    }

    ensureStreamingReady(); // rings exist before any streamed buffer becomes visible
    samplePool_.endBatch();

    userSampleRefs_ = std::move(loadedRefs);
    swapPlayableMap(patch.map);
    undoManager_.clearUndoHistory();
    editState_.reset(false);
    hasUserInstrument_.store(!patch.map.zones.empty());
    demoLoaded_ = false;
    patchName_ = juce::String(patch.name.empty() ? "Untitled" : patch.name);

    // Restore APVTS snapshot when present
    for (const auto& kv : patch.params)
    {
        if (auto* param = apvts_.getParameter(kv.first))
        {
            const float denorm = static_cast<float>(kv.second);
            // convertTo0to1 expects the real-world value for AudioParameterFloat/Choice
            param->setValueNotifyingHost(param->convertTo0to1(denorm));
        }
    }
    // Patch field wins for RR mode (older patches without it load as Cycle).
    setRoundRobinMode(patch.map.rrMode);

    syncParamsToEngine();
    return true;
}

bool LooperAudioProcessor::loadPatchFromFile(const juce::File& file, juce::StringArray* missingPathsOut)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto loaded = PatchStore::load(file.getFullPathName().toStdString());
    if (!loaded)
        return false;

    // applyPatch re-checks existence and fills missingPathsOut / offlineSampleIds_
    const bool ok = applyPatch(loaded->patch, missingPathsOut);
    if (ok)
    {
        lastPatchPath_ = file.getFullPathName();
        editState_.markSaved();
    }
    return ok;
}

std::vector<looper::MissingSample> LooperAudioProcessor::missingSamples() const
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    std::vector<looper::MissingSample> out;
    out.reserve(offlineSampleIds_.size());
    for (const auto& id : offlineSampleIds_)
    {
        const auto it = std::find_if(userSampleRefs_.begin(), userSampleRefs_.end(),
                                     [&](const SampleRef& r) { return r.id == id; });
        out.push_back({ id, it != userSampleRefs_.end() ? it->path : std::string() });
    }
    return out;
}

bool LooperAudioProcessor::relocateSample(const std::string& sampleId, const juce::File& newFile,
                                          juce::String* error)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto it = std::find_if(userSampleRefs_.begin(), userSampleRefs_.end(),
                                 [&](const SampleRef& r) { return r.id == sampleId; });
    if (it == userSampleRefs_.end())
    {
        if (error != nullptr) *error = "Unknown sample";
        return false;
    }
    if (! newFile.existsAsFile())
    {
        if (error != nullptr) *error = "File not found";
        return false;
    }

    // Decodes into a fresh buffer, then swaps it into the pool under the pool lock:
    // zones that point at this id play the new audio from the next note-on.
    auto loaded = importController_.loadFileIntoPool(newFile, samplePool_, *it);
    if (! loaded.ok)
    {
        if (error != nullptr) *error = loaded.error.isNotEmpty() ? loaded.error : juce::String("Could not decode");
        return false;
    }

    ensureStreamingReady();
    *it = loaded.ref; // new absolute path; PatchStore::save rewrites it relative to the patch
    offlineSampleIds_.erase(std::remove(offlineSampleIds_.begin(), offlineSampleIds_.end(), sampleId),
                            offlineSampleIds_.end());
    editState_.markExternalChange(); // not undoable: unsaved until the next save
    return true;
}

bool LooperAudioProcessor::performZoneEdit(size_t index, const looper::Zone& proposed,
                                           const juce::String& actionName, bool newTransaction)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto edit = looper::makeZoneEdit(copyInstrumentMap(), index, proposed);
    if (! edit)
        return false;
    if (newTransaction)
        undoManager_.beginNewTransaction(actionName);
    return undoManager_.perform(new looper::ZoneEditAction(*this, std::move(*edit), &editState_));
}

bool LooperAudioProcessor::performZoneEdits(const std::vector<looper::ZoneChange>& changes,
                                            const juce::String& actionName, bool newTransaction)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto edits = looper::makeZoneEdits(copyInstrumentMap(), changes);
    if (edits.empty())
        return false;
    if (newTransaction)
        undoManager_.beginNewTransaction(actionName);
    return undoManager_.perform(new looper::ZoneEditAction(*this, std::move(edits), &editState_));
}

bool LooperAudioProcessor::replaceZones(const std::vector<looper::ZoneEdit>& edits, bool forward)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto next = std::make_shared<InstrumentMap>(copyInstrumentMap());
    if (! looper::applyZoneEdits(*next, edits, forward))
        return false;
    InstrumentMap retired = *next;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        std::swap(map_, retired);
        mapShared_ = next;
    }
    voiceEngine_.updateMapLive(next); // one hand-off for the whole group
    return true;
}

bool LooperAudioProcessor::replaceZone(size_t index, const std::string& sampleId, const looper::Zone& zone)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    // Copy + edit outside the lock; the lock only covers O(1) swaps.
    auto next = std::make_shared<InstrumentMap>(copyInstrumentMap());
    if (! looper::replaceZoneInMap(*next, index, sampleId, zone))
        return false;
    InstrumentMap retired = *next;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        std::swap(map_, retired);
        mapShared_ = next;
    }
    voiceEngine_.updateMapLive(next); // audio thread adopts it next block; sounding voices follow
    return true;
}

const SampleRef* LooperAudioProcessor::findSampleRef(const std::string& sampleId) const
{
    const auto it = std::find_if(userSampleRefs_.begin(), userSampleRefs_.end(),
                                 [&](const SampleRef& r) { return r.id == sampleId; });
    return it != userSampleRefs_.end() ? &*it : nullptr;
}

void LooperAudioProcessor::auditionZone(int zoneIndex, bool down)
{
    const auto map = copyInstrumentMap();
    if (! down || ! juce::isPositiveAndBelow(zoneIndex, (int) map.zones.size()))
    {
        if (auditioning_)
            voiceEngine_.stopAudition();
        auditioning_ = false;
        return;
    }
    // Targets the zone itself (VoiceEngine::requestAudition), not a MIDI note: overlapping
    // zones and round-robin selection cannot swap in a different sample.
    int note = 60, velocity = 100;
    looper::auditionNoteFor(map.zones[(size_t) zoneIndex], note, velocity);
    voiceEngine_.requestAudition(zoneIndex, note, velocity);
    auditioning_ = true;
}

looper::RoundRobinMode LooperAudioProcessor::currentRoundRobinMode() const
{
    if (auto* raw = apvts_.getRawParameterValue("rrMode"))
        return raw->load() >= 0.5f ? looper::RoundRobinMode::Random : looper::RoundRobinMode::Cycle;
    return looper::RoundRobinMode::Cycle;
}

void LooperAudioProcessor::setRoundRobinMode(looper::RoundRobinMode mode)
{
    if (auto* param = apvts_.getParameter("rrMode"))
        param->setValueNotifyingHost(param->convertTo0to1(mode == looper::RoundRobinMode::Random ? 1.0f : 0.0f));
}

void LooperAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Host state: APVTS ValueTree + embedded patch JSON blob (paths as currently known).
    juce::ValueTree root("LooperState");
    root.setProperty("schemaVersion", PatchStore::kSchemaVersion, nullptr);
    root.setProperty("patchName", patchName_, nullptr);
    root.setProperty("lastPatchPath", lastPatchPath_, nullptr);
    root.setProperty("prefsJson", juce::String(prefs_.toJson()), nullptr);
    root.addChild(apvts_.copyState(), -1, nullptr);

    const auto patchJson = PatchStore::toJson(buildCurrentPatch());
    root.setProperty("patchJson", juce::String(patchJson), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void LooperAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml)
        return;

    // Legacy: bare APVTS state
    if (xml->hasTagName(apvts_.state.getType()))
    {
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
        syncParamsToEngine();
        return;
    }

    if (!xml->hasTagName("LooperState"))
        return;

    auto root = juce::ValueTree::fromXml(*xml);
    patchName_ = root.getProperty("patchName", "Untitled").toString();
    lastPatchPath_ = root.getProperty("lastPatchPath", "").toString();

    const auto prefsJson = root.getProperty("prefsJson", "").toString();
    if (prefsJson.isNotEmpty())
    {
        if (auto loaded = SessionPrefs::fromJson(prefsJson.toStdString()))
            prefs_ = *loaded;
    }

    if (auto paramsTree = root.getChildWithName(apvts_.state.getType()); paramsTree.isValid())
        apvts_.replaceState(paramsTree);

    const auto patchJson = root.getProperty("patchJson", "").toString();
    if (patchJson.isNotEmpty())
    {
        if (auto patch = PatchStore::fromJson(patchJson.toStdString()))
        {
            // Paths in host state may be absolute (from last session) or relative to lastPatchPath.
            if (lastPatchPath_.isNotEmpty())
            {
                const auto dir = PatchStore::parentDirectory(lastPatchPath_.toStdString());
                PatchStore::resolveSamplePaths(*patch, dir);
            }
            applyPatch(*patch, nullptr);
            editState_.markSaved();
        }
    }

    applyPrefsToRuntime();
    syncParamsToEngine();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LooperAudioProcessor();
}
