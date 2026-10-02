#pragma once

#include "../AmpEnv/AmpEnv.h"
#include "../Filter/SvfFilter.h"
#include "../InstrumentMap/InstrumentMap.h"
#include "../SamplePool/SamplePool.h"
#include "FastRng.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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
    /** Current playback ratio (may be gliding toward targetPitchRatio). */
    double pitchRatio = 1.0;
    double targetPitchRatio = 1.0;
    /** Per-sample increment toward targetPitchRatio (0 when not gliding). */
    double glideInc = 0.0;
    double fileToHostRatio = 1.0;
    float velocityAmp = 1.0f;
    float zoneGainLin = 1.0f;
    Zone zone;
    std::shared_ptr<const SampleBuffer> buffer;
    AmpEnv ampEnv;
    SvfFilter filter; // dual-state stereo SVF
    bool releasing = false;
    /** Key physically held (noteOn without matching noteOff yet). */
    bool gated = false;
    /** Deferred release while sustain pedal is down. */
    bool pedalHeld = false;
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
 * Zone pick: velocity layers + round-robin per rrGroup (Cycle or Random, see RoundRobinMode).
 * Legato portamento when InstrumentMap.glideMs > 0; CC64 sustain via setSustainPedal.
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

    /** Resets RR cycle counters and Random "last played" memory. Message thread. */
    void clearRrCounters();

    /**
     * Round-robin mode for groups with 2+ alternates. Cycle (default) is the v1 behaviour.
     * Lock-free (relaxed atomic): may be called from the audio thread every block.
     * The engine does not read InstrumentMap::rrMode; the host/processor pushes it here.
     */
    void setRoundRobinMode(RoundRobinMode mode) noexcept { rrMode_.store(mode, std::memory_order_relaxed); }
    RoundRobinMode roundRobinMode() const noexcept { return rrMode_.load(std::memory_order_relaxed); }

    /** Re-seed the Random-mode PRNG (tests: deterministic sequences). Not audio-thread safe. */
    void setRandomSeed(uint64_t seed) noexcept { rng_.seedWith(seed); }

    /** Alternates beyond this many in one RR group are ignored (stack scratch, no allocation). */
    static constexpr size_t kMaxRrAlternates = 128;

    void setSamplePool(SamplePool* pool) { pool_ = pool; }

    void setEnvParams(const AmpEnv::Params& p);
    void setFilterParams(const FilterParams& p);
    void setMasterGainLin(float g) { masterGainLin_ = g; }

    /** Soft-clip master bus after voice sum (tanh ceiling). */
    void setMasterSoftClip(bool on) { masterSoftClip_ = on; }

    /** Extra cutoff offset in octaves (e.g. mod wheel); applied after env mod. */
    void setModCutoffOffsetOctaves(float oct) { modCutoffOctaves_ = oct; }

    /** Pitch bend in semitones (±2 default range applied by MidiRouter). */
    void setPitchBendSemis(float semis);

    /**
     * Sustain pedal (CC64). While down, noteOff defers ampEnv release (pedalHeld).
     * On pedal up, releases voices that are not still gated.
     */
    void setSustainPedal(bool down);
    bool sustainPedal() const { return sustainPedal_; }

    void noteOn(int note, int velocity, int channel);
    void noteOff(int note, int channel);
    void allNotesOff();

    void processBlock(float* left, float* right, int numSamples);

    int activeVoiceCount() const;
    float pitchBendSemis() const { return pitchBendSemis_; }
    const FilterParams& filterParams() const { return filterParams_; }

    /**
     * First active voice matching note/channel, or nullptr.
     * Intended for tests / diagnostics.
     */
    const Voice* findActiveVoice(int note, int channel) const;

    /**
     * Velocity-layer + round-robin zone selection.
     * Returns nullptr if no zone matches (caller may use demo fallback).
     * Group = zones matching note/velocity that share the first match's non-zero rrGroup.
     * Cycle: advances the rrGroup's counter (rrIndex order). Random: uniform pick that never
     * repeats the group's previous alternate (2+ alternates). A 1-zone group just plays.
     * Real-time safe: no allocation, no locks.
     */
    const Zone* selectZone(int note, int velocity);

private:
    int allocateVoice(int note, int channel);
    void startVoice(int voiceIndex, int note, int velocity, int channel, const Zone& zone,
                    std::shared_ptr<const SampleBuffer> buffer);
    void updateVoicePitchRatio(Voice& v) const;
    void applyFilterGlobals(Voice& v) const;
    double computePitchRatio(int note, const Zone& zone) const;
    bool channelHasHeldNote(int channel, int excludeVoiceIndex = -1) const;
    double newestHeldPitchRatio(int channel, int excludeVoiceIndex = -1) const;
    void setupGlide(Voice& v, double targetRatio, int channel, int voiceIndex);

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
    bool sustainPedal_ = false;

    /**
     * Fixed-size open-addressing table (no audio-thread allocation).
     * key (rrGroup, lead = -1): Cycle counter per rrGroup (v1 semantics).
     * key (rrGroup, lead = lowest map index of the group's alternates): last played zone,
     * i.e. per keyzone / velocity-layer group, used by Random to avoid back-to-back repeats.
     */
    struct RrSlot
    {
        bool used = false;
        int32_t group = 0;
        int32_t lead = -1;
        uint32_t counter = 0;
        int32_t lastZone = -1;
    };
    static constexpr size_t kRrSlots = 1024; // power of two
    RrSlot& rrSlot(int group, int lead) noexcept;
    std::array<RrSlot, kRrSlots> rrSlots_ {};
    std::atomic<RoundRobinMode> rrMode_ { RoundRobinMode::Cycle };
    FastRng rng_;
};

} // namespace looper
