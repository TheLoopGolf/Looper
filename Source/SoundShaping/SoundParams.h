#pragma once

#include "../AmpEnv/AmpEnv.h"
#include "../Filter/SvfFilter.h"
#include "../MidiRouter/MidiRouter.h"
#include "../VoiceEngine/VoiceEngine.h"

#include <array>
#include <cmath>
#include <string>

namespace looper::sound {

/**
 * Sound-shaping parameters (amp / filter / filter envelope / velocity / bend) shared by the
 * plugin's APVTS and the unit tests (no JUCE here). The processor reads its raw parameter values,
 * builds a SoundParams and pushes it to the engine every block with applyToEngine().
 *
 * Parameter ids are part of saved patches ("params" object) and host sessions: never rename.
 */
namespace pid {
inline constexpr const char* attack = "attack";
inline constexpr const char* decay = "decay";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* release = "release";
inline constexpr const char* volume = "volume";
inline constexpr const char* filterType = "filterType";       // 0 LP12, 1 HP, 2 BP, 3 LP24 (v1: 0..2)
inline constexpr const char* cutoff = "cutoff";
inline constexpr const char* resonance = "resonance";
inline constexpr const char* filterEnvAmt = "filterEnvAmt";   // v1 amp env -> cutoff (octaves), legacy
// Added in the sound-shaping release
inline constexpr const char* fenvAttack = "fenvAttack";
inline constexpr const char* fenvDecay = "fenvDecay";
inline constexpr const char* fenvSustain = "fenvSustain";
inline constexpr const char* fenvRelease = "fenvRelease";
inline constexpr const char* fenvAmount = "fenvAmount";       // semitones, bipolar
inline constexpr const char* keyTrack = "keyTrack";           // percent
inline constexpr const char* velAmp = "velAmp";               // percent
inline constexpr const char* velCutoff = "velCutoff";         // semitones, bipolar
inline constexpr const char* velAttack = "velAttack";         // percent
inline constexpr const char* bendUp = "bendUp";               // semitones
inline constexpr const char* bendDown = "bendDown";           // semitones
} // namespace pid

struct ParamDefault
{
    const char* id;
    float value;
};

/** Every parameter the plugin saves in a patch's "params" object (order = save order). */
inline constexpr std::array<const char*, 20> kPatchParamIds { {
    pid::attack, pid::decay, pid::sustain, pid::release, pid::volume,
    pid::filterType, pid::cutoff, pid::resonance, pid::filterEnvAmt,
    pid::fenvAttack, pid::fenvDecay, pid::fenvSustain, pid::fenvRelease, pid::fenvAmount,
    pid::keyTrack, pid::velAmp, pid::velCutoff, pid::velAttack, pid::bendUp, pid::bendDown,
} };

/**
 * Parameters added after v1 with the value an older patch / session gets when it does not
 * mention them. These values reproduce the v1 sound exactly (amounts off, full velocity -> amp,
 * +/-2 semitone bend). The filter-envelope times are ordinary defaults: they are inaudible while
 * fenvAmount is 0.
 */
inline constexpr std::array<ParamDefault, 11> kAddedParams { {
    { pid::fenvAttack, 1.0f },
    { pid::fenvDecay, 400.0f },
    { pid::fenvSustain, 0.0f },
    { pid::fenvRelease, 300.0f },
    { pid::fenvAmount, 0.0f },
    { pid::keyTrack, 0.0f },
    { pid::velAmp, 100.0f },
    { pid::velCutoff, 0.0f },
    { pid::velAttack, 0.0f },
    { pid::bendUp, 2.0f },
    { pid::bendDown, 2.0f },
} };

inline bool isAddedParam(const std::string& id)
{
    for (const auto& p : kAddedParams)
        if (id == p.id)
            return true;
    return false;
}

inline float addedParamDefault(const std::string& id, float fallback = 0.0f)
{
    for (const auto& p : kAddedParams)
        if (id == p.id)
            return p.value;
    return fallback;
}

/** Choice index of "filterType" -> engine type (unknown -> LP12, as in v1). */
inline FilterType filterTypeFromIndex(int index)
{
    switch (index)
    {
        case 1:  return FilterType::HighPass;
        case 2:  return FilterType::BandPass;
        case 3:  return FilterType::LowPass24;
        default: return FilterType::LowPass;
    }
}

/** Same as juce::Decibels::decibelsToGain (float, -100 dB floor) so tests match the plugin. */
inline float decibelsToGain(float db)
{
    return db > -100.0f ? std::pow(10.0f, db * 0.05f) : 0.0f;
}

struct SoundParams
{
    AmpEnv::Params amp;
    float volumeDb = 0.0f;
    FilterParams filter;
    AmpEnv::Params filterEnv { 1.0f, 400.0f, 0.0f, 300.0f };
    VelocityParams velocity;
    float bendUpSemis = 2.0f;
    float bendDownSemis = 2.0f;
};

/**
 * Build from raw (denormalised) parameter values. `get(id)` returns the value as float; the
 * caller decides what a missing id means (the plugin always has every parameter).
 */
template <typename Get>
SoundParams fromRawValues(Get&& get)
{
    SoundParams s;
    s.amp.attackMs = get(pid::attack);
    s.amp.decayMs = get(pid::decay);
    s.amp.sustain = get(pid::sustain);
    s.amp.releaseMs = get(pid::release);
    s.volumeDb = get(pid::volume);

    s.filter.type = filterTypeFromIndex(static_cast<int>(get(pid::filterType)));
    s.filter.cutoffHz = get(pid::cutoff);
    s.filter.resonance = get(pid::resonance);
    s.filter.envAmount = get(pid::filterEnvAmt);
    s.filter.fenvAmountSemis = get(pid::fenvAmount);
    s.filter.keyTrack = get(pid::keyTrack) / 100.0f;
    s.filter.velToCutoffSemis = get(pid::velCutoff);

    s.filterEnv.attackMs = get(pid::fenvAttack);
    s.filterEnv.decayMs = get(pid::fenvDecay);
    s.filterEnv.sustain = get(pid::fenvSustain);
    s.filterEnv.releaseMs = get(pid::fenvRelease);

    s.velocity.toAmp = get(pid::velAmp) / 100.0f;
    s.velocity.toAttack = get(pid::velAttack) / 100.0f;

    s.bendUpSemis = get(pid::bendUp);
    s.bendDownSemis = get(pid::bendDown);
    return s;
}

/** Push to the engine / router (audio thread, every block; cheap and allocation-free). */
inline void applyToEngine(const SoundParams& s, VoiceEngine& engine, MidiRouter& router)
{
    engine.setEnvParams(s.amp);
    engine.setMasterGainLin(decibelsToGain(s.volumeDb));
    engine.setFilterParams(s.filter);
    engine.setFilterEnvParams(s.filterEnv);
    engine.setVelocityParams(s.velocity);
    router.setBendRange(s.bendUpSemis, s.bendDownSemis);
}

} // namespace looper::sound
