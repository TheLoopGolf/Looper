// FROZEN REFERENCE - do not edit. Verbatim copy of Source/VoiceEngine/VoiceEngine.cpp at commit 3eb8926 (pre sound-shaping),
// renamed into namespace looper_v1. SoundShapingTests renders old patches through this engine and
// through the current one and requires bit-identical output (backward compatibility).
#include "VoiceEngineV1.h"
#include "VoiceEngine/Hermite.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace looper {}
namespace looper_v1 {
using namespace looper;

namespace {

float dbToLin(float db)
{
    return std::pow(10.0f, db * 0.05f);
}

} // namespace

VoiceEngine::VoiceEngine()
{
    voices_.resize(static_cast<size_t>(polyphony_));
}

VoiceEngine::~VoiceEngine()
{
    for (auto& v : voices_)
        endVoiceStream(v);
    if (pool_ != nullptr)
        pool_->unregisterReader(poolReader_);
}

void VoiceEngine::setSamplePool(SamplePool* pool)
{
    if (pool == pool_)
        return;
    if (pool_ != nullptr)
        pool_->unregisterReader(poolReader_);
    pool_ = pool;
    poolReader_ = pool_ != nullptr ? pool_->registerReader() : -1;
}

std::shared_ptr<const SampleBuffer> VoiceEngine::lookupBuffer(const std::string& id) const noexcept
{
    if (pool_ == nullptr)
        return {};
    if (poolReader_ >= 0)
    {
        // Lock-free: pin the published snapshot, copy the shared_ptr (atomic increment).
        if (const PoolSnapshot* snap = pool_->pin(poolReader_))
            if (const auto* found = snap->find(id))
                return *found;
        return {};
    }
    // More than SamplePool::kMaxReaders engines on one pool: locking fallback.
    return pool_->getBuffer(id);
}

void VoiceEngine::endVoiceStream(Voice& v) noexcept
{
    if (v.streamSlot >= 0 && streamer_ != nullptr)
        streamer_->release(v.streamSlot);
    v.streamSlot = -1;
    v.streamWin = {};
}

StreamingEngineStats VoiceEngine::streamingStats() const noexcept
{
    StreamingEngineStats st;
    st.streamingVoices = streamingVoices_.load(std::memory_order_relaxed);
    st.underruns = underruns_.load(std::memory_order_relaxed);
    return st;
}

void VoiceEngine::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
    for (auto& v : voices_)
    {
        v.ampEnv.setSampleRate(sampleRate_);
        v.filter.setSampleRate(sampleRate_);
    }
}

void VoiceEngine::setPolyphony(int n)
{
    polyphony_ = std::clamp(n, 1, 128);
    for (size_t i = static_cast<size_t>(polyphony_); i < voices_.size(); ++i)
        endVoiceStream(voices_[i]); // dropped voices give their stream slots back
    voices_.resize(static_cast<size_t>(polyphony_));
    for (auto& v : voices_)
    {
        v.ampEnv.setSampleRate(sampleRate_);
        v.ampEnv.setParams(envParams_);
        v.filter.setSampleRate(sampleRate_);
        applyFilterGlobals(v);
    }
}

void VoiceEngine::setEnvParams(const AmpEnv::Params& p)
{
    envParams_ = p;
    for (auto& v : voices_)
        v.ampEnv.setParams(envParams_);
}

void VoiceEngine::setFilterParams(const FilterParams& p)
{
    filterParams_ = p;
    for (auto& v : voices_)
        applyFilterGlobals(v);
}

void VoiceEngine::applyFilterGlobals(Voice& v) const
{
    v.filter.setType(filterParams_.type);
    v.filter.setCutoffHz(filterParams_.cutoffHz);
    v.filter.setResonance(filterParams_.resonance);
}

void VoiceEngine::setPitchBendSemis(float semis)
{
    pitchBendSemis_ = semis;
    for (auto& v : voices_)
    {
        if (v.active)
            updateVoicePitchRatio(v);
    }
}

double VoiceEngine::computePitchRatio(int note, const Zone& zone) const
{
    const double semis = static_cast<double>(note) + static_cast<double>(pitchBendSemis_)
                       + static_cast<double>(zone.coarseTranspose)
                       + static_cast<double>(zone.tuneCents) / 100.0
                       - static_cast<double>(zone.rootKey);
    return std::pow(2.0, semis / 12.0);
}

void VoiceEngine::updateVoicePitchRatio(Voice& v) const
{
    // Pitch bend updates target immediately; if not mid-glide, snap current too.
    const double newTarget = computePitchRatio(v.note, v.zone);
    const bool gliding = (v.glideInc != 0.0)
                         && (v.pitchRatio != v.targetPitchRatio);
    v.targetPitchRatio = newTarget;
    if (!gliding)
    {
        v.pitchRatio = newTarget;
        v.glideInc = 0.0;
    }
    else
    {
        // Keep current; recompute linear glide remainder over remaining distance
        // using original glideMs from map if available.
        const float glideMs = (map_ != nullptr) ? map_->glideMs : 0.0f;
        if (glideMs <= 0.0f || sampleRate_ <= 0.0)
        {
            v.pitchRatio = newTarget;
            v.glideInc = 0.0;
        }
        else
        {
            const double samples = std::max(1.0, sampleRate_ * static_cast<double>(glideMs) * 0.001);
            v.glideInc = (newTarget - v.pitchRatio) / samples;
        }
    }
}

bool VoiceEngine::channelHasHeldNote(int channel, int excludeVoiceIndex) const
{
    for (int i = 0; i < polyphony_; ++i)
    {
        if (i == excludeVoiceIndex)
            continue;
        const auto& v = voices_[static_cast<size_t>(i)];
        if (v.active && v.channel == channel && !v.releasing && (v.gated || v.pedalHeld))
            return true;
    }
    return false;
}

double VoiceEngine::newestHeldPitchRatio(int channel, int excludeVoiceIndex) const
{
    double ratio = 1.0;
    uint64_t bestAge = 0;
    bool found = false;
    for (int i = 0; i < polyphony_; ++i)
    {
        if (i == excludeVoiceIndex)
            continue;
        const auto& v = voices_[static_cast<size_t>(i)];
        if (!v.active || v.channel != channel || v.releasing)
            continue;
        if (!(v.gated || v.pedalHeld))
            continue;
        if (!found || v.age > bestAge)
        {
            bestAge = v.age;
            ratio = v.pitchRatio;
            found = true;
        }
    }
    return ratio;
}

void VoiceEngine::setupGlide(Voice& v, double targetRatio, int channel, int voiceIndex)
{
    v.targetPitchRatio = targetRatio;
    const float glideMs = (map_ != nullptr) ? map_->glideMs : 0.0f;
    const bool legato = glideMs > 0.0f && channelHasHeldNote(channel, voiceIndex);
    if (!legato)
    {
        v.pitchRatio = targetRatio;
        v.glideInc = 0.0;
        return;
    }

    const double fromRatio = newestHeldPitchRatio(channel, voiceIndex);
    v.pitchRatio = fromRatio;
    const double samples = std::max(1.0, sampleRate_ * static_cast<double>(glideMs) * 0.001);
    v.glideInc = (targetRatio - fromRatio) / samples;
}

void VoiceEngine::setMap(const InstrumentMap* map)
{
    mapHold_.reset();
    map_ = map;
    clearRrCounters();
}

void VoiceEngine::adoptMap(std::shared_ptr<const InstrumentMap> map)
{
    {
        // A wholesale swap supersedes any queued live edit.
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingMapReady_.store(false, std::memory_order_release);
        pendingMap_.reset();
    }
    map_ = map.get();
    mapHold_ = std::move(map);
    clearRrCounters();
}

void VoiceEngine::updateMapLive(std::shared_ptr<const InstrumentMap> map)
{
    if (!map)
        return;
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingMap_ = std::move(map); // drops a previously queued (or retired) map here, off the audio thread
    pendingMapReady_.store(true, std::memory_order_release);
}

bool VoiceEngine::applyPendingMapUpdate() noexcept
{
    if (!pendingMapReady_.load(std::memory_order_acquire))
        return false;
    std::unique_lock<std::mutex> lock(pendingMutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false; // message thread is queueing; pick it up next block
    if (!pendingMapReady_.load(std::memory_order_relaxed) || !pendingMap_)
        return false;
    const size_t oldCount = map_ != nullptr ? map_->zones.size() : 0;
    // Swap so the old map is parked in pendingMap_ and freed by the message thread later.
    std::swap(mapHold_, pendingMap_);
    map_ = mapHold_.get();
    pendingMapReady_.store(false, std::memory_order_relaxed);
    lock.unlock();

    if (map_ == nullptr || map_->zones.size() != oldCount)
        clearRrCounters();
    refreshVoicesFromMap();
    return true;
}

void VoiceEngine::refreshVoicesFromMap() noexcept
{
    if (map_ == nullptr)
        return;
    const auto& zones = map_->zones;
    for (auto& v : voices_)
    {
        if (!v.active || v.zoneIndex < 0 || static_cast<size_t>(v.zoneIndex) >= zones.size())
            continue;
        const Zone& z = zones[static_cast<size_t>(v.zoneIndex)];
        if (z.sampleId != v.zone.sampleId)
            continue;
        const bool pitchChanged = z.rootKey != v.zone.rootKey || z.tuneCents != v.zone.tuneCents
                                  || z.coarseTranspose != v.zone.coarseTranspose;
        v.zone.rootKey = z.rootKey;
        v.zone.tuneCents = z.tuneCents;
        v.zone.coarseTranspose = z.coarseTranspose;
        v.zone.pan = z.pan;
        if (z.gainDb != v.zone.gainDb)
        {
            // Glide (~20 ms) instead of stepping: a gain edit on a sounding note must not click.
            v.zone.gainDb = z.gainDb;
            v.zoneGainLin = dbToLin(z.gainDb);
            v.zoneGain.setTarget(v.zoneGainLin, GainRamp::samplesFor(sampleRate_));
        }
        // Key / velocity / RR fields only matter at note-on; keep the voice's copy in sync.
        v.zone.keyLow = z.keyLow;
        v.zone.keyHigh = z.keyHigh;
        v.zone.velLow = z.velLow;
        v.zone.velHigh = z.velHigh;
        v.zone.rrGroup = z.rrGroup;
        v.zone.rrIndex = z.rrIndex;
        if (pitchChanged)
            updateVoicePitchRatio(v);
    }
}

void VoiceEngine::mapChanged()
{
    clearRrCounters();
}

void VoiceEngine::clearRrCounters()
{
    for (auto& slot : rrSlots_)
        slot = RrSlot{};
}

VoiceEngine::RrSlot& VoiceEngine::rrSlot(int group, int lead) noexcept
{
    // Integer mix of (group, lead) -> home slot; linear probe. If the table is ever full
    // (more than kRrSlots distinct groups played) groups share the home slot: RR still
    // works, only the per-group memory degrades.
    uint32_t h = static_cast<uint32_t>(group) * 0x9E3779B1u ^ (static_cast<uint32_t>(lead) + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    const size_t home = static_cast<size_t>(h) & (kRrSlots - 1);
    for (size_t probe = 0; probe < kRrSlots; ++probe)
    {
        auto& slot = rrSlots_[(home + probe) & (kRrSlots - 1)];
        if (!slot.used)
        {
            slot.used = true;
            slot.group = group;
            slot.lead = lead;
            slot.counter = 0;
            slot.lastZone = -1;
            return slot;
        }
        if (slot.group == group && slot.lead == lead)
            return slot;
    }
    return rrSlots_[home];
}

const Zone* VoiceEngine::selectZone(int note, int velocity)
{
    if (map_ == nullptr || map_->zones.empty())
        return nullptr;

    const auto& zones = map_->zones;
    const size_t numZones = zones.size();

    // First match in stable map order decides the group.
    size_t first = numZones;
    for (size_t i = 0; i < numZones; ++i)
    {
        if (zones[i].matchesNoteVelocity(note, velocity))
        {
            first = i;
            break;
        }
    }
    if (first == numZones)
        return nullptr;

    const int bestGroup = zones[first].rrGroup;

    // rrGroup == 0 means no RR: first stable match wins (even if duplicates).
    if (bestGroup == 0)
        return &zones[first];

    // Alternates = matching zones in the same rrGroup (stack scratch, no allocation).
    std::array<uint32_t, kMaxRrAlternates> group;
    size_t count = 0;
    for (size_t i = first; i < numZones && count < kMaxRrAlternates; ++i)
    {
        if (zones[i].rrGroup == bestGroup && zones[i].matchesNoteVelocity(note, velocity))
            group[count++] = static_cast<uint32_t>(i);
    }

    if (count <= 1)
        return &zones[first];

    // Sort by rrIndex ascending, stable tie-break by map order.
    std::sort(group.begin(), group.begin() + static_cast<std::ptrdiff_t>(count),
              [&zones](uint32_t a, uint32_t b) {
                  const int ia = zones[a].rrIndex;
                  const int ib = zones[b].rrIndex;
                  if (ia != ib)
                      return ia < ib;
                  return a < b;
              });

    // Per keyzone / velocity-layer memory of the last alternate played (both modes record it,
    // so switching Cycle -> Random never repeats the note that just sounded).
    RrSlot& memory = rrSlot(bestGroup, static_cast<int>(first));

    size_t pick = 0;
    if (rrMode_.load(std::memory_order_relaxed) == RoundRobinMode::Random)
    {
        size_t lastPos = count;
        for (size_t k = 0; k < count; ++k)
        {
            if (static_cast<int32_t>(group[k]) == memory.lastZone)
            {
                lastPos = k;
                break;
            }
        }
        if (lastPos < count)
        {
            // Uniform over the other count-1 alternates: draw in [0, count-1), skip lastPos.
            pick = rng_.nextBelow(static_cast<uint32_t>(count - 1));
            if (pick >= lastPos)
                ++pick;
        }
        else
        {
            pick = rng_.nextBelow(static_cast<uint32_t>(count));
        }
    }
    else
    {
        // Cycle mode (v1): per-rrGroup counter, next counter % size.
        RrSlot& cycle = rrSlot(bestGroup, -1);
        pick = static_cast<size_t>(cycle.counter % static_cast<uint32_t>(count));
        ++cycle.counter;
    }

    memory.lastZone = static_cast<int32_t>(group[pick]);
    return &zones[group[pick]];
}

int VoiceEngine::allocateVoice(int note, int channel)
{
    // Same note/channel retrigger: steal that voice first
    for (int i = 0; i < polyphony_; ++i)
    {
        auto& v = voices_[static_cast<size_t>(i)];
        if (v.active && v.note == note && v.channel == channel)
            return i;
    }

    // Free voice
    for (int i = 0; i < polyphony_; ++i)
    {
        if (!voices_[static_cast<size_t>(i)].active)
            return i;
    }

    // Steal: prefer releasing / quietest (lowest env gain), then oldest.
    // Never prefer the most recent note if avoidable (skip max age).
    int best = -1;
    float bestLevel = std::numeric_limits<float>::max();
    uint64_t bestAge = std::numeric_limits<uint64_t>::max();
    uint64_t newestAge = 0;
    for (int i = 0; i < polyphony_; ++i)
    {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (v.active && v.age > newestAge)
            newestAge = v.age;
    }

    // Pass 1: releasing voices
    for (int i = 0; i < polyphony_; ++i)
    {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (!v.active || !v.releasing)
            continue;
        if (v.age == newestAge && polyphony_ > 1)
            continue;
        const float lvl = v.ampEnv.level();
        if (lvl < bestLevel || (lvl == bestLevel && v.age < bestAge))
        {
            bestLevel = lvl;
            bestAge = v.age;
            best = i;
        }
    }
    if (best >= 0)
        return best;

    // Pass 2: any non-newest voice by quietest then oldest
    best = -1;
    bestLevel = std::numeric_limits<float>::max();
    bestAge = std::numeric_limits<uint64_t>::max();
    for (int i = 0; i < polyphony_; ++i)
    {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (!v.active)
            continue;
        if (v.age == newestAge && polyphony_ > 1)
            continue;
        const float lvl = v.ampEnv.level();
        if (lvl < bestLevel || (lvl == bestLevel && v.age < bestAge))
        {
            bestLevel = lvl;
            bestAge = v.age;
            best = i;
        }
    }
    if (best >= 0)
        return best;

    // Last resort: oldest
    best = 0;
    bestAge = voices_[0].age;
    for (int i = 1; i < polyphony_; ++i)
    {
        if (voices_[static_cast<size_t>(i)].age < bestAge)
        {
            bestAge = voices_[static_cast<size_t>(i)].age;
            best = i;
        }
    }
    return best;
}

void VoiceEngine::startVoice(int voiceIndex, int note, int velocity, int channel, const Zone& zone,
                             std::shared_ptr<const SampleBuffer> buffer, int zoneIndex)
{
    auto& v = voices_[static_cast<size_t>(voiceIndex)];
    v.active = true;
    v.zoneIndex = zoneIndex;
    v.note = note;
    v.velocity = velocity;
    v.channel = channel;
    v.age = ++ageCounter_;
    v.readPos = zone.sampleStart.has_value() ? static_cast<double>(*zone.sampleStart) : 0.0;
    v.zone = zone;
    endVoiceStream(v);            // stolen / retriggered voice frees its old stream slot first
    v.buffer = std::move(buffer); // previous buffer: refcount decrement only (pool keeps it alive)
    v.streaming = v.buffer && v.buffer->isStreaming();
    v.residentFrames = v.buffer ? v.buffer->residentLength() : 0;
    v.streamGain = 1.0f;
    v.starved = false;
    v.holdL = v.holdR = 0.0f;
    if (v.streaming && streamer_ != nullptr)
    {
        // Ring starts where the preload ends: the voice plays the preload instantly meanwhile.
        v.streamSlot = streamer_->acquire(v.buffer, static_cast<int64_t>(std::floor(v.readPos)) - 1);
        if (v.streamSlot < 0)
            streamer_->noteExhausted();
    }
    v.releasing = false;
    v.gated = true;
    v.pedalHeld = false;
    float velLin = std::clamp(velocity, 1, 127) / 127.0f;
    if (map_ != nullptr)
    {
        switch (map_->velCurve)
        {
            case VelCurve::Soft: velLin = std::sqrt(velLin); break;
            case VelCurve::Hard: velLin = velLin * velLin; break;
            default: break;
        }
    }
    v.velocityAmp = velLin;
    v.zoneGainLin = dbToLin(zone.gainDb);
    v.zoneGain.reset(v.zoneGainLin);
    v.fileToHostRatio = (v.buffer && sampleRate_ > 0.0)
                            ? (v.buffer->sampleRate / sampleRate_)
                            : 1.0;
    v.ampEnv.setSampleRate(sampleRate_);
    v.ampEnv.setParams(envParams_);
    v.ampEnv.noteOn(false);
    v.filter.setSampleRate(sampleRate_);
    applyFilterGlobals(v);
    v.filter.reset();

    const double target = computePitchRatio(note, zone);
    setupGlide(v, target, channel, voiceIndex);
}

void VoiceEngine::noteOn(int note, int velocity, int channel)
{
    if (velocity <= 0)
    {
        noteOff(note, channel);
        return;
    }

    Zone zone;
    std::shared_ptr<const SampleBuffer> buffer;
    int zoneIndex = -1;

    if (const Zone* z = selectZone(note, velocity))
    {
        zone = *z;
        zoneIndex = static_cast<int>(z - map_->zones.data());
        buffer = lookupBuffer(zone.sampleId);
        // Offline zone (sample file missing, awaiting relocation): stay silent rather
        // than substituting the demo tone.
        if (!buffer && zone.sampleId != kDemoSampleId)
            return;
    }

    // Fallback: demo sample at root 60 when no map/zones match
    if (!buffer)
    {
        zoneIndex = -1;
        zone = makeDemoZone();
        zone.rootKey = 60;
        // The host installs the demo tone up front (no decoding / allocation here).
        buffer = lookupBuffer(kDemoSampleId);
    }

    if (!buffer || buffer->length <= 0 || buffer->interleaved.empty())
        return;

    const int idx = allocateVoice(note, channel);
    startVoice(idx, note, velocity, channel, zone, std::move(buffer), zoneIndex);
}

namespace {
// auditionRequest_ layout: [63..35] seq | [34] on | [33..14] zone | [13..7] note | [6..0] velocity
constexpr uint64_t kAudSeqShift = 35;
constexpr uint64_t kAudOnBit = 1ull << 34;
constexpr uint64_t kAudZoneShift = 14;
constexpr uint64_t kAudZoneMask = (1ull << 20) - 1;
} // namespace

void VoiceEngine::requestAudition(int zoneIndex, int note, int velocity) noexcept
{
    if (zoneIndex < 0 || static_cast<uint64_t>(zoneIndex) > kAudZoneMask)
    {
        stopAudition();
        return;
    }
    const uint64_t seq = ++auditionSeqIssued_;
    const uint64_t packed = (seq << kAudSeqShift) | kAudOnBit
                            | (static_cast<uint64_t>(zoneIndex) << kAudZoneShift)
                            | (static_cast<uint64_t>(std::clamp(note, 0, 127)) << 7)
                            | static_cast<uint64_t>(std::clamp(velocity, 1, 127));
    auditionRequest_.store(packed, std::memory_order_release);
}

void VoiceEngine::stopAudition() noexcept
{
    const uint64_t seq = ++auditionSeqIssued_;
    auditionRequest_.store(seq << kAudSeqShift, std::memory_order_release);
}

void VoiceEngine::applyAuditionRequest() noexcept
{
    const uint64_t req = auditionRequest_.load(std::memory_order_acquire);
    const uint64_t seq = req >> kAudSeqShift;
    if (seq == auditionSeqSeen_)
        return;
    auditionSeqSeen_ = seq;
    // Release whatever the previous request started
    for (auto& v : voices_)
    {
        if (v.active && v.channel == kAuditionChannel && !v.releasing)
        {
            v.gated = false;
            v.pedalHeld = false;
            v.ampEnv.noteOff();
            v.releasing = true;
        }
    }
    if ((req & kAudOnBit) == 0)
        return;
    const int zoneIndex = static_cast<int>((req >> kAudZoneShift) & kAudZoneMask);
    const int note = static_cast<int>((req >> 7) & 0x7f);
    const int velocity = static_cast<int>(req & 0x7f);
    auditionZoneNow(zoneIndex, note, velocity);
}

bool VoiceEngine::auditionZoneNow(int zoneIndex, int note, int velocity)
{
    if (map_ == nullptr || zoneIndex < 0 || static_cast<size_t>(zoneIndex) >= map_->zones.size() || pool_ == nullptr)
        return false;
    const Zone& zone = map_->zones[static_cast<size_t>(zoneIndex)];
    auto buffer = lookupBuffer(zone.sampleId);
    if (!buffer || buffer->length <= 0 || buffer->interleaved.empty())
        return false; // offline sample: stay silent
    const int idx = allocateVoice(note, kAuditionChannel);
    startVoice(idx, note, std::max(1, velocity), kAuditionChannel, zone, std::move(buffer), zoneIndex);
    return true;
}

void VoiceEngine::noteOff(int note, int channel)
{
    for (auto& v : voices_)
    {
        if (!v.active || v.note != note || v.channel != channel)
            continue;
        if (v.releasing)
            continue;

        v.gated = false;
        if (sustainPedal_)
        {
            v.pedalHeld = true;
            // Defer ampEnv.noteOff until pedal up
        }
        else
        {
            v.pedalHeld = false;
            v.ampEnv.noteOff();
            v.releasing = true;
        }
    }
}

void VoiceEngine::setSustainPedal(bool down)
{
    if (sustainPedal_ && !down)
    {
        // Pedal release: noteOff all deferred voices whose keys are no longer held
        for (auto& v : voices_)
        {
            if (!v.active || v.releasing)
                continue;
            if (v.pedalHeld && !v.gated)
            {
                v.ampEnv.noteOff();
                v.releasing = true;
                v.pedalHeld = false;
            }
            else if (v.gated)
            {
                v.pedalHeld = false;
            }
        }
    }
    sustainPedal_ = down;
}

void VoiceEngine::allNotesOff()
{
    sustainPedal_ = false;
    for (auto& v : voices_)
    {
        if (v.active)
        {
            v.gated = false;
            v.pedalHeld = false;
            v.ampEnv.noteOff();
            v.releasing = true;
        }
    }
}

void VoiceEngine::processBlock(float* left, float* right, int numSamples)
{
    applyPendingMapUpdate();
    applyAuditionRequest();

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }

    if (numSamples <= 0)
        return;

    const float envAmt = filterParams_.envAmount;
    const float baseCutoff = filterParams_.cutoffHz;
    const bool modulateCutoff = (envAmt != 0.0f) || (modCutoffOctaves_ != 0.0f);

    // Sub-blocks keep stream progress fresh for the reader threads (and bound how far a voice
    // reads ahead between publishes). Voices are independent per sample, so the mix is
    // bit-identical to rendering the block in one pass.
    for (int offset = 0; offset < numSamples; offset += kSubBlock)
    {
        const int n = std::min(kSubBlock, numSamples - offset);
        for (auto& v : voices_)
        {
            if (!v.active || !v.buffer)
                continue;
            renderVoice(v, left + offset, right + offset, n, modulateCutoff, envAmt, baseCutoff);
        }
    }

    int streamingVoices = 0;
    for (auto& v : voices_)
    {
        if (!v.active)
        {
            endVoiceStream(v);
            // Drop the finished voice's buffer reference (refcount decrement only: the pool keeps
            // every buffer until its message-thread GC), so replaced audio can be freed promptly.
            if (v.buffer)
                v.buffer.reset();
            continue;
        }
        if (v.streaming)
            ++streamingVoices;
    }
    streamingVoices_.store(streamingVoices, std::memory_order_relaxed);

    if (masterSoftClip_)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = std::tanh(left[i]);
            right[i] = std::tanh(right[i]);
        }
    }
}

void VoiceEngine::renderVoice(Voice& v, float* left, float* right, int numSamples, bool modulateCutoff,
                              float envAmt, float baseCutoff) noexcept
{
    const auto& buf = *v.buffer;
    const int64_t endFrame = v.zone.sampleEnd.has_value()
                                 ? std::min(buf.length, *v.zone.sampleEnd)
                                 : buf.length;
    const float pan = std::clamp(v.zone.pan, -1.0f, 1.0f);
    const float baseL = v.velocityAmp * masterGainLin_ * (0.5f * (1.0f - pan));
    const float baseR = v.velocityAmp * masterGainLin_ * (0.5f * (1.0f + pan));

    if (v.streaming && v.streamSlot < 0 && streamer_ != nullptr
        && v.readPos + 4096.0 >= static_cast<double>(v.residentFrames))
    {
        // Started while every slot was busy: try again before the preload runs out.
        v.streamSlot = streamer_->acquire(v.buffer, static_cast<int64_t>(std::floor(v.readPos)) - 1);
    }

    bool pastEnd = false;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance legato glide sample-by-sample toward targetPitchRatio
        if (v.glideInc != 0.0 && v.pitchRatio != v.targetPitchRatio)
        {
            v.pitchRatio += v.glideInc;
            if ((v.glideInc > 0.0 && v.pitchRatio >= v.targetPitchRatio)
                || (v.glideInc < 0.0 && v.pitchRatio <= v.targetPitchRatio))
            {
                v.pitchRatio = v.targetPitchRatio;
                v.glideInc = 0.0;
            }
        }

        float env = v.ampEnv.process();
        if (!v.ampEnv.isActive())
        {
            v.active = false;
            break;
        }

        if (v.readPos >= static_cast<double>(endFrame))
        {
            pastEnd = true;
            // Keep rendering silence through release if needed
            if (!v.releasing)
            {
                v.ampEnv.noteOff();
                v.releasing = true;
                v.gated = false;
                v.pedalHeld = false;
                env = v.ampEnv.level();
            }
        }

        float sL = 0.0f, sR = 0.0f;
        if (!pastEnd && v.readPos < static_cast<double>(endFrame))
        {
            if (v.streaming)
                readStreamed(v, sL, sR);
            else
                hermiteRead(buf.interleaved.data(), buf.channels, buf.length, v.readPos, sL, sR);

            // Amp-env -> filter: reuse amp ADSR to modulate cutoff in octaves
            if (modulateCutoff)
            {
                const float oct = envAmt * env + modCutoffOctaves_;
                const float cutoff = baseCutoff * std::exp2(oct);
                v.filter.setCutoffHz(cutoff);
            }

            v.filter.processStereo(sL, sR);
        }

        // Amp after filter (gain from same env); zone gain ramps after live edits
        const float zg = v.zoneGain.next();
        left[i] += sL * env * baseL * zg;
        right[i] += sR * env * baseR * zg;
        v.readPos += v.pitchRatio * v.fileToHostRatio;
    }

    if (!v.ampEnv.isActive())
        v.active = false;

    if (v.streamSlot >= 0 && streamer_ != nullptr)
    {
        if (!v.active)
            endVoiceStream(v);
        else
            streamer_->publishProgress(v.streamSlot,
                                       std::max<int64_t>(0, static_cast<int64_t>(std::floor(v.readPos)) - 1),
                                       std::max(v.pitchRatio, v.targetPitchRatio) * v.fileToHostRatio);
    }
}

namespace {
/** Underrun fade length: ~1.5 ms at 44.1 kHz (64 samples). */
constexpr float kStreamFadeStep = 1.0f / 64.0f;
} // namespace

void VoiceEngine::readStreamed(Voice& v, float& outL, float& outR) noexcept
{
    const SampleBuffer& buf = *v.buffer;
    const double pos = v.readPos;
    const int64_t i1 = static_cast<int64_t>(std::floor(pos));
    const int64_t res = v.residentFrames;
    float l = 0.0f, r = 0.0f;
    bool ok = true;

    if (i1 + 2 < res)
    {
        // Entirely inside the RAM preload: the exact same read as a fully loaded sample.
        hermiteRead(buf.interleaved.data(), buf.channels, res, pos, l, r);
    }
    else
    {
        // Straddles the preload/stream boundary or is fully streamed: gather the 4 Hermite
        // neighbours frame by frame (preload, ring, or zero past the end) and interpolate with
        // the same arithmetic as hermiteStereo / hermiteMono, so the output is bit-identical.
        const float t = static_cast<float>(pos - static_cast<double>(i1));
        const int ch = buf.channels >= 2 ? 2 : 1;
        float y[2][4] = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        for (int k = 0; k < 4 && ok; ++k)
        {
            const int64_t f = i1 - 1 + k;
            if (f < 0 || f >= buf.length)
                continue; // zero-padded edge
            const float* src = nullptr;
            if (f < res)
                src = buf.interleaved.data() + f * ch;
            else
            {
                if (!v.streamWin.contains(f) && v.streamSlot >= 0 && streamer_ != nullptr)
                {
                    v.streamWin = streamer_->window(v.streamSlot);
                    if (!v.streamWin.contains(f) && nonRealtime_.load(std::memory_order_relaxed))
                    {
                        // Offline bounce: wait for the disk instead of dropping out.
                        streamer_->fillBlocking(v.streamSlot, std::max<int64_t>(0, i1 - 1), i1 + 3);
                        v.streamWin = streamer_->window(v.streamSlot);
                    }
                }
                if (v.streamWin.contains(f))
                    src = v.streamWin.frame(f);
            }
            if (src == nullptr)
            {
                ok = false;
                break;
            }
            y[0][k] = src[0];
            y[1][k] = ch >= 2 ? src[1] : src[0];
        }
        if (ok)
        {
            l = hermite4(y[0][0], y[0][1], y[0][2], y[0][3], t);
            r = ch >= 2 ? hermite4(y[1][0], y[1][1], y[1][2], y[1][3], t) : l;
        }
    }

    if (ok)
    {
        v.holdL = l;
        v.holdR = r;
        v.starved = false;
        if (v.streamGain < 1.0f)
        {
            // Recovering from an underrun: fade back in.
            v.streamGain = std::min(1.0f, v.streamGain + kStreamFadeStep);
            l *= v.streamGain;
            r *= v.streamGain;
        }
    }
    else
    {
        if (!v.starved)
        {
            v.starved = true;
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
        // Fade the last good frame out instead of jumping to silence (no click).
        v.streamGain = std::max(0.0f, v.streamGain - kStreamFadeStep);
        l = v.holdL * v.streamGain;
        r = v.holdR * v.streamGain;
    }
    outL = l;
    outR = r;
}

int VoiceEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices_)
        if (v.active)
            ++n;
    return n;
}

const Voice* VoiceEngine::findActiveVoice(int note, int channel) const
{
    for (const auto& v : voices_)
    {
        if (v.active && v.note == note && v.channel == channel)
            return &v;
    }
    return nullptr;
}

} // namespace looper_v1
