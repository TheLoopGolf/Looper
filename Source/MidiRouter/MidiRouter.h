#pragma once

#include "../InstrumentMap/InstrumentMap.h"
#include "../VoiceEngine/VoiceEngine.h"

#include <algorithm>

namespace looper {

/**
 * MIDI → voice engine router.
 * Pitch bend: ±2 semitones (14-bit, center 8192).
 * CC1 mod wheel → FilterCutoff (octaves offset) when target is FilterCutoff.
 * CC64 sustain: value >= 64 = on; VoiceEngine defers noteOff until pedal up.
 */
class MidiRouter
{
public:
    void setEngine(VoiceEngine* engine) { engine_ = engine; }

    void setModWheelTarget(ModWheelTarget t)
    {
        modTarget_ = t;
        applyModWheel();
    }

    void handleNoteOn(int note, int velocity, int channel)
    {
        if (engine_ && velocity > 0)
            engine_->noteOn(note, velocity, channel);
        else if (engine_)
            engine_->noteOff(note, channel);
    }

    void handleNoteOff(int note, int channel)
    {
        if (engine_)
            engine_->noteOff(note, channel);
    }

    void handlePitchBend(int value14, int /*channel*/)
    {
        if (!engine_)
            return;
        // 0..16383, center 8192 → ±bendRangeSemis_
        const float norm = (static_cast<float>(value14) - 8192.0f) / 8192.0f;
        engine_->setPitchBendSemis(norm * bendRangeSemis_);
    }

    void handleCc(int cc, int value, int /*channel*/)
    {
        if (!engine_)
            return;

        if (cc == 1) // mod wheel
        {
            modWheel_ = std::clamp(value, 0, 127) / 127.0f;
            applyModWheel();
        }
        else if (cc == 64) // sustain pedal
        {
            engine_->setSustainPedal(value >= 64);
        }
    }

    void setBendRangeSemis(float semis) { bendRangeSemis_ = semis; }

private:
    void applyModWheel()
    {
        if (!engine_)
            return;
        if (modTarget_ == ModWheelTarget::FilterCutoff)
            engine_->setModCutoffOffsetOctaves(modWheel_ * 2.0f); // 0..+2 octaves
        else
            engine_->setModCutoffOffsetOctaves(0.0f);
    }

    VoiceEngine* engine_ = nullptr;
    float bendRangeSemis_ = 2.0f;
    ModWheelTarget modTarget_ = ModWheelTarget::FilterCutoff;
    float modWheel_ = 0.0f;
};

} // namespace looper
