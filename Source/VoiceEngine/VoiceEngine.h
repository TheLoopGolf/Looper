#pragma once

#include "../AmpEnv/AmpEnv.h"
#include "../Filter/SvfFilter.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "../SamplePool/SamplePool.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace looper {

struct Voice
{
    bool active = false;
    int note = 60;
    int velocity = 100;
    int channel = 0;
    uint64_t age = 0;
    double readPos = 0.0;
    double pitchRatio = 1.0; // note+bend relative to root (semitone ratio)
    double fileToHostRatio = 1.0;
    float velocityAmp = 1.0f;
    float zoneGainLin = 1.0f;
    Zone zone;
    std::shared_ptr<const SampleBuffer> buffer;
    AmpEnv ampEnv;
    SvfFilter filter; // dual-state stereo SVF
    bool releasing = false;
};

struct FilterParams
{
    FilterType type = FilterType::LowPass;
    float cutoffHz = 12000.0f;
    float resonance = 0.2f;   // 0..1
    float envAmount = 0.0f;   // −1..1 octaves at full amp env
};

/**
 * Polyphonic voice engine: Hermite resample + AmpEnv ADSR + per-voice SVF.
 * Filter envelope reuses amp ADSR (cutoff *= 2^(envAmount * env)).
 * Zone pick: velocity layers + round-robin cycle per rrGroup.
 */
class VoiceEngine
{
public:
    VoiceEngine();

    void setSampleRate(double sr);
    void setPolyphony(int n);

    /** Sets the active map and clears RR counters (map content may have changed). */
    void setMap(const InstrumentMap* map);

    /**
     * Take shared ownership of a map (message-thread swap).
     * Keeps the InstrumentMap alive while the audio thread may still read it.
     * Prefer this over setMap when replacing maps from the UI / import path.
     */
    void adoptMap(std::shared_ptr<const InstrumentMap> map);

    /** Call when map zones changed in-place (same pointer). Clears RR counters. */
    void mapChanged();

    void clearRrCounters();

    void setSamplePool(SamplePool* pool) { pool_ = pool; }

    void setEnvParams(const AmpEnv::Params& p);
    void setFilterParams(const FilterParams& p);
    void setMasterGainLin(float g) { masterGainLin_ = g; }

    /** Soft-clip master bus after voice sum (tanh ceiling). */
    void setMasterSoftClip(bool on) { masterSoftClip_ = on; }

    /**
     * TODO(glide): InstrumentMap.glideMs is stored from Settings; portamento / legato
     * glide is not applied in the voice engine yet. Wire glideMs here when implementing.
     */

    /** Extra cutoff offset in octaves (e.g. mod wheel); applied after env mod. */
    void setModCutoffOffsetOctaves(float oct) { modCutoffOctaves_ = oct; }

    /** Pitch bend in semitones (±2 default range applied by MidiRouter). */
    void setPitchBendSemis(float semis);

    void noteOn(int note, int velocity, int channel);
    void noteOff(int note, int channel);
    void allNotesOff();

    void processBlock(float* left, float* right, int numSamples);

    int activeVoiceCount() const;
    float pitchBendSemis() const { return pitchBendSemis_; }
    const FilterParams& filterParams() const { return filterParams_; }

    /**
     * Velocity-layer + round-robin zone selection.
     * Returns nullptr if no zone matches (caller may use demo fallback).
     * Advances RR cycle counters for non-zero rrGroups when a group has >1 zone.
     */
    const Zone* selectZone(int note, int velocity);

private:
    int allocateVoice(int note, int channel);
    void startVoice(int voiceIndex, int note, int velocity, int channel, const Zone& zone,
                    std::shared_ptr<const SampleBuffer> buffer);
    void updateVoicePitchRatio(Voice& v) const;
    void applyFilterGlobals(Voice& v) const;
    double computePitchRatio(int note, const Zone& zone) const;

    double sampleRate_ = 44100.0;
    int polyphony_ = 64;
    std::vector<Voice> voices_;
    const InstrumentMap* map_ = nullptr;
    std::shared_ptr<const InstrumentMap> mapHold_;
    SamplePool* pool_ = nullptr;
    AmpEnv::Params envParams_;
    FilterParams filterParams_;
    float masterGainLin_ = 1.0f;
    bool masterSoftClip_ = false;
    float pitchBendSemis_ = 0.0f;
    float modCutoffOctaves_ = 0.0f;
    uint64_t ageCounter_ = 0;

    /** Per-rrGroup cycle counters (rrGroup == 0 unused). */
    std::unordered_map<int, uint32_t> rrCounters_;
};

} // namespace looper
