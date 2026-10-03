#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Import/MapCommit.h"
#include "../ZoneEdit/ZoneEditAction.h"
#include "../SoundShaping/SoundParams.h"

#include <algorithm>
#include <cmath>

namespace {
namespace pid = looper::sound::pid;

juce::String formatSemis (float v, int)
{
    if (std::abs (v) < 0.005f)
        return "0 st";
    return (v > 0.0f ? "+" : "") + juce::String (v, std::abs (v) < 10.0f ? 1 : 0) + " st";
}

juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }

juce::String formatMs (float v, int)
{
    if (v >= 1000.0f)
        return juce::String (v / 1000.0f, 2) + " s";
    return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
}

float parseNumber (const juce::String& text) { return text.retainCharacters ("+-.0123456789").getFloatValue(); }

/** Accepts what formatMs prints ("850 ms", "1.20 s") as well as a bare number of milliseconds. */
float parseMs (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const bool seconds = t.endsWithChar ('s') && ! t.endsWith ("ms");
    return parseNumber (t) * (seconds ? 1000.0f : 1.0f);
}

juce::String formatDb (float v, int)
{
    if (std::abs (v) < 0.05f)
        return "0.0 dB";
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
}

juce::String formatHz (float v, int)
{
    if (v >= 1000.0f)
        return juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}

/** "1.5 kHz", "1.5k" and "850" (Hz) all parse. */
float parseHz (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    return parseNumber (t) * (t.containsChar ('k') ? 1000.0f : 1.0f);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LooperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // Display / text-entry formats only: ranges, ids and defaults are unchanged from v1. The
    // strings carry their unit, so no separate label (hosts would show it twice).
    auto msAttr = juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction(formatMs)
                      .withValueFromStringFunction(parseMs);
    auto semisAttr = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction(formatSemis)
                         .withValueFromStringFunction(parseNumber);
    auto pctAttr = juce::AudioParameterFloatAttributes()
                       .withStringFromValueFunction(formatPercent)
                       .withValueFromStringFunction(parseNumber);
    auto dbAttr = juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction(formatDb)
                      .withValueFromStringFunction(parseNumber);
    auto hzAttr = juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction(formatHz)
                      .withValueFromStringFunction(parseHz);
    auto unitPctAttr = juce::AudioParameterFloatAttributes()   // 0..1 shown as 0..100%
                           .withStringFromValueFunction([] (float v, int) { return formatPercent (v * 100.0f, 0); })
                           .withValueFromStringFunction([] (const juce::String& t) { return parseNumber (t) / 100.0f; });
    auto bendAttr = juce::AudioParameterIntAttributes()
                        .withStringFromValueFunction([] (int v, int) { return juce::String (v) + " st"; })
                        .withValueFromStringFunction([] (const juce::String& t) { return juce::roundToInt (parseNumber (t)); });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack",
        juce::NormalisableRange<float>(0.1f, 5000.0f, 0.01f, 0.35f), 1.0f, msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"decay", 1}, "Decay",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.01f, 0.35f), 100.0f, msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sustain", 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f, unitPctAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1}, "Release",
        juce::NormalisableRange<float>(1.0f, 8000.0f, 0.01f, 0.35f), 200.0f, msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"volume", 1}, "Volume",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.01f), 0.0f, dbAttr));
    // Index order is stored in patches / sessions: 0..2 are v1, "Low Pass 24" was appended.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filterType", 1}, "Filter Type",
        juce::StringArray{"Low Pass 12", "High Pass", "Band Pass", "Low Pass 24"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cutoff", 1}, "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.3f), 12000.0f, hzAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"resonance", 1}, "Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f, unitPctAttr));
    // v1 "Filter Env": the amp envelope modulating cutoff (octaves). Kept so old patches and
    // automation sound the same; new patches use the dedicated filter envelope below.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterEnvAmt", 1}, "Amp Env > Cutoff (legacy)",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("oct")));

    // --- Sound shaping (v2). Defaults reproduce v1 (see looper::sound::kAddedParams). ---
    using looper::sound::addedParamDefault;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::fenvAttack, 2}, "Filter Env Attack",
        juce::NormalisableRange<float>(0.1f, 5000.0f, 0.01f, 0.35f), addedParamDefault(pid::fenvAttack), msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::fenvDecay, 2}, "Filter Env Decay",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.01f, 0.35f), addedParamDefault(pid::fenvDecay), msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::fenvSustain, 2}, "Filter Env Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), addedParamDefault(pid::fenvSustain), unitPctAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::fenvRelease, 2}, "Filter Env Release",
        juce::NormalisableRange<float>(1.0f, 8000.0f, 0.01f, 0.35f), addedParamDefault(pid::fenvRelease), msAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::fenvAmount, 2}, "Filter Env Amount",
        juce::NormalisableRange<float>(-60.0f, 60.0f, 0.01f), addedParamDefault(pid::fenvAmount), semisAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::keyTrack, 2}, "Filter Key Track",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), addedParamDefault(pid::keyTrack), pctAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::velAmp, 2}, "Velocity > Amp",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), addedParamDefault(pid::velAmp), pctAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::velCutoff, 2}, "Velocity > Cutoff",
        juce::NormalisableRange<float>(-60.0f, 60.0f, 0.01f), addedParamDefault(pid::velCutoff), semisAttr));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{pid::velAttack, 2}, "Velocity > Attack",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), addedParamDefault(pid::velAttack), pctAttr));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{pid::bendUp, 2}, "Pitch Bend Up", 0, 48, (int) addedParamDefault(pid::bendUp),
        bendAttr));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{pid::bendDown, 2}, "Pitch Bend Down", 0, 48, (int) addedParamDefault(pid::bendDown),
        bendAttr));
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
    // Pitch-bend range is a per-patch parameter now (bendUp / bendDown, pushed every block).
    midiRouter_.setModWheelTarget(prefs_.modWheelTarget);

    // Push map-global fields onto the live map when present
    auto next = copyInstrumentMap();
    pushPrefsOntoMap(next);
    swapPlayableMap(std::move(next));
}

void LooperAudioProcessor::applySessionPrefs(const SessionPrefs& prefs)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const bool defaultFilterChanged = prefs.defaultFilterType != prefs_.defaultFilterType;
    prefs_ = prefs;
    prefs_.polyphony = juce::jlimit(1, 128, prefs_.polyphony);
    prefs_.glideMs = juce::jmax(0.0f, prefs_.glideMs);
    prefs_.preloadFrames = juce::jlimit(SessionPrefs::kMinPreloadFrames, SessionPrefs::kMaxPreloadFrames,
                                        prefs_.preloadFrames);
    applyPrefsToRuntime();
    // Settings > Default filter type applies to the current patch when the user changes it. It is
    // no longer re-applied on every prefs push (that used to override a restored session's filter).
    if (defaultFilterChanged)
        if (auto* param = apvts_.getParameter(pid::filterType))
            param->setValueNotifyingHost(param->convertTo0to1(
                static_cast<float>(juce::jlimit(0, 3, prefs_.defaultFilterType))));
    syncParamsToEngine();
    reloadSamplesForStreaming(); // no-op unless the preload size changed
}

void LooperAudioProcessor::syncParamsToEngine()
{
    // Amp / filter / filter env / velocity / bend: one mapping shared with the unit tests.
    const auto sp = looper::sound::fromRawValues([this] (const char* id) {
        auto* raw = apvts_.getRawParameterValue(id);
        jassert(raw != nullptr);
        return raw != nullptr ? raw->load() : 0.0f;
    });
    looper::sound::applyToEngine(sp, voiceEngine_, midiRouter_);
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

    // Snapshot host-automatable sound parameters (restored on load; missing ones get v1 values).
    for (auto* id : looper::sound::kPatchParamIds)
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
    // Patches saved before the sound-shaping release: the new parameters take the values that
    // reproduce the old sound (not whatever the previous patch left behind).
    resetMissingAddedParams([&patch] (const std::string& id) { return patch.params.count(id) > 0; });
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
        const auto tree = juce::ValueTree::fromXml(*xml);
        apvts_.replaceState(tree);
        resetMissingAddedParams([&tree] (const std::string& id) {
            return tree.getChildWithProperty("id", juce::String(id)).isValid();
        });
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
    {
        apvts_.replaceState(paramsTree);
        // APVTS keeps the current value for ids the saved tree lacks: give them their v1 values.
        resetMissingAddedParams([&paramsTree] (const std::string& id) {
            return paramsTree.getChildWithProperty("id", juce::String(id)).isValid();
        });
    }

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

void LooperAudioProcessor::resetMissingAddedParams(const std::function<bool(const std::string&)>& has)
{
    for (const auto& added : looper::sound::kAddedParams)
    {
        if (has(added.id))
            continue;
        float value = added.value;
        // v1 bend range lived in session prefs (always +/-2 from the UI): carry it over.
        if (std::string(added.id) == pid::bendUp || std::string(added.id) == pid::bendDown)
            value = (float) juce::jlimit(0, 48, juce::roundToInt(prefs_.pitchBendRangeSemis));
        if (auto* param = apvts_.getParameter(added.id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    }
}

void LooperAudioProcessor::convertLegacyFilterEnv()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto raw = [this] (const char* id) { return apvts_.getRawParameterValue(id)->load(); };
    const float legacyOct = raw(pid::filterEnvAmt);
    if (juce::exactlyEqual(legacyOct, 0.0f))
        return;
    auto set = [this] (const char* id, float value) {
        if (auto* param = apvts_.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    if (juce::exactlyEqual(raw(pid::fenvAmount), 0.0f))
    {
        // Same shape (amp ADSR), same depth in semitones.
        set(pid::fenvAttack, raw(pid::attack));
        set(pid::fenvDecay, raw(pid::decay));
        set(pid::fenvSustain, raw(pid::sustain));
        set(pid::fenvRelease, raw(pid::release));
        set(pid::fenvAmount, juce::jlimit(-60.0f, 60.0f, legacyOct * 12.0f));
    }
    set(pid::filterEnvAmt, 0.0f); // the old amp-env modulation is switched off
    if (hasUserInstrument_.load())
        editState_.markExternalChange();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LooperAudioProcessor();
}
