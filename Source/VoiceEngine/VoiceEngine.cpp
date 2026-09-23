#include "VoiceEngine.h"
#include "Hermite.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace looper {

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
    map_ = map.get();
    mapHold_ = std::move(map);
    clearRrCounters();
}

void VoiceEngine::mapChanged()
{
    clearRrCounters();
}

void VoiceEngine::clearRrCounters()
{
    rrCounters_.clear();
}

const Zone* VoiceEngine::selectZone(int note, int velocity)
{
    if (map_ == nullptr || map_->zones.empty())
        return nullptr;

    const auto indices = map_->matchingZoneIndices(note, velocity);
    if (indices.empty())
        return nullptr;

    // Best group = rrGroup of the first match in stable map order.
    const int bestGroup = map_->zones[indices.front()].rrGroup;

    std::vector<size_t> groupIndices;
    groupIndices.reserve(indices.size());
    for (size_t idx : indices)
    {
        if (map_->zones[idx].rrGroup == bestGroup)
            groupIndices.push_back(idx);
    }

    if (groupIndices.empty())
        return &map_->zones[indices.front()];

    // rrGroup == 0 means no RR: first stable match wins (even if duplicates).
    if (bestGroup == 0 || groupIndices.size() == 1)
        return &map_->zones[groupIndices.front()];

    // Cycle mode: sort by rrIndex ascending, pick next counter % size.
    std::sort(groupIndices.begin(), groupIndices.end(),
              [this](size_t a, size_t b) {
                  const int ia = map_->zones[a].rrIndex;
                  const int ib = map_->zones[b].rrIndex;
                  if (ia != ib)
                      return ia < ib;
                  return a < b; // stable tie-break by map order
              });

    uint32_t& counter = rrCounters_[bestGroup];
    const size_t pick = static_cast<size_t>(counter % static_cast<uint32_t>(groupIndices.size()));
    ++counter;
    return &map_->zones[groupIndices[pick]];
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
                             std::shared_ptr<const SampleBuffer> buffer)
{
    auto& v = voices_[static_cast<size_t>(voiceIndex)];
    v.active = true;
    v.note = note;
    v.velocity = velocity;
    v.channel = channel;
    v.age = ++ageCounter_;
    v.readPos = zone.sampleStart.has_value() ? static_cast<double>(*zone.sampleStart) : 0.0;
    v.zone = zone;
    v.buffer = std::move(buffer);
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

    if (const Zone* z = selectZone(note, velocity))
    {
        zone = *z;
        if (pool_)
            buffer = pool_->getBuffer(zone.sampleId);
    }

    // Fallback: demo sample at root 60 when no map/zones or missing buffer
    if (!buffer)
    {
        zone = makeDemoZone();
        zone.rootKey = 60;
        if (pool_)
            buffer = pool_->getBuffer(kDemoSampleId);
        if (!buffer && pool_)
        {
            pool_->loadDemoSample(sampleRate_);
            buffer = pool_->getBuffer(kDemoSampleId);
        }
    }

    if (!buffer || buffer->length <= 0 || buffer->interleaved.empty())
        return;

    const int idx = allocateVoice(note, channel);
    startVoice(idx, note, velocity, channel, zone, std::move(buffer));
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

    for (auto& v : voices_)
    {
        if (!v.active || !v.buffer)
            continue;

        const auto& buf = *v.buffer;
        const int64_t endFrame = v.zone.sampleEnd.has_value()
                                     ? std::min(buf.length, *v.zone.sampleEnd)
                                     : buf.length;
        const float pan = std::clamp(v.zone.pan, -1.0f, 1.0f);
        const float gainL = v.velocityAmp * v.zoneGainLin * masterGainLin_ * (0.5f * (1.0f - pan));
        const float gainR = v.velocityAmp * v.zoneGainLin * masterGainLin_ * (0.5f * (1.0f + pan));

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
                hermiteRead(buf.interleaved.data(), buf.channels, buf.length, v.readPos, sL, sR);

                // Amp-env → filter: reuse amp ADSR to modulate cutoff in octaves
                if (modulateCutoff)
                {
                    const float oct = envAmt * env + modCutoffOctaves_;
                    const float cutoff = baseCutoff * std::exp2(oct);
                    v.filter.setCutoffHz(cutoff);
                }

                v.filter.processStereo(sL, sR);
            }

            // Amp after filter (gain from same env)
            left[i] += sL * env * gainL;
            right[i] += sR * env * gainR;
            v.readPos += v.pitchRatio * v.fileToHostRatio;
        }

        if (!v.ampEnv.isActive())
            v.active = false;
    }

    if (masterSoftClip_)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = std::tanh(left[i]);
            right[i] = std::tanh(right[i]);
        }
    }
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

} // namespace looper
