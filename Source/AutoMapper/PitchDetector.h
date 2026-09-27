#pragma once

#include <cstddef>
#include <string>

namespace looper {

/**
 * Tunables for YIN pitch detection. Defaults target musical samples
 * (A0 27.5 Hz .. C8 ~4.2 kHz) and are what AutoMapper import uses.
 */
struct PitchDetectorConfig
{
    double minHz = 27.5;            // A0
    double maxHz = 4200.0;          // just above C8 (4186 Hz)
    double yinThreshold = 0.12;     // absolute threshold on the CMND function
    double skipAfterOnsetMs = 70.0; // skip attack transient before analysing
    double maxAnalysisSec = 1.5;    // only look at this much audio after the skip
    int maxFrames = 9;              // frames analysed (median taken)
    double silenceDb = -60.0;       // peak below this → silent / unpitched
    float unpitchedBelowConfidence = 0.5f;
};

/** Result of analysing one sample. JUCE-free, plain data. */
struct PitchAnalysis
{
    bool analysed = false;          // false = no audio given / not run
    bool unpitched = true;          // true for silence, noise, drums, too short
    double f0Hz = 0.0;              // median fundamental across voiced frames
    int midiNote = -1;              // nearest MIDI note (A4 = 69 = 440 Hz, C4 = 60)
    float cents = 0.0f;             // f0 offset from midiNote, -50..+50
    float confidence = 0.0f;        // 0..1 (periodicity × frame agreement)
    int framesAnalysed = 0;
    int framesVoiced = 0;
    std::string reason;             // short human-readable note ("silent", "noise", ...)
};

class PitchDetector
{
public:
    /**
     * Analyse an interleaved float buffer (any channel count; mixed to mono).
     * numFrames = samples per channel. Safe for empty / tiny / silent input.
     */
    static PitchAnalysis analyzeInterleaved(const float* interleaved,
                                            std::size_t numFrames,
                                            int numChannels,
                                            double sampleRate,
                                            const PitchDetectorConfig& cfg = {});

    /** Mono convenience overload. */
    static PitchAnalysis analyzeMono(const float* mono, std::size_t numSamples,
                                     double sampleRate,
                                     const PitchDetectorConfig& cfg = {})
    {
        return analyzeInterleaved(mono, numSamples, 1, sampleRate, cfg);
    }

    /** f0 → fractional MIDI note (69 = A4 = 440 Hz). */
    static double hzToMidi(double hz);
};

} // namespace looper
