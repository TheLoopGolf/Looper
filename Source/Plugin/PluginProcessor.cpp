#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Import/MapCommit.h"

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
    applyPrefsToRuntime();
    ensureDemoInstrument();
}

LooperAudioProcessor::~LooperAudioProcessor() = default;

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
        out.push_back({ z.keyLow, z.keyHigh });
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
    return importController_.importFiles(filesOrFolders, samplePool_, userSampleRefs_,
                                         prefs_.toAutoMapOptions())
           && importController_.hasPending();
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
    applyPrefsToRuntime();
    syncParamsToEngine();
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
    }
    return ok;
}

bool LooperAudioProcessor::applyPatch(const Patch& patch, juce::StringArray* missingPathsOut)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    offlineSampleIds_.clear();
    std::vector<SampleRef> loadedRefs;
    loadedRefs.reserve(patch.samples.size());

    for (const auto& refIn : patch.samples)
    {
        SampleRef ref = refIn;
        if (ref.path.find("://") != std::string::npos)
        {
            // Special / demo URI — keep metadata, no file load
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
            samplePool_.addStub(ref); // keep zone metadata; buffer absent → silent/offline
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

    userSampleRefs_ = std::move(loadedRefs);
    swapPlayableMap(patch.map);
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
        lastPatchPath_ = file.getFullPathName();
    return ok;
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
        }
    }

    applyPrefsToRuntime();
    syncParamsToEngine();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LooperAudioProcessor();
}
