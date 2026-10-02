#pragma once

#include <optional>
#include <string>
#include <vector>

namespace looper {

struct FilenameTokens
{
    std::optional<int> midiNote;       // 0–127 when resolved from name
    std::optional<int> velocityHint;   // 1–127
    std::optional<int> rrIndex;        // 1-based from filename when present
    std::string normalizedStem;        // lowercase, no extension
    std::string originalStem;
};

/**
 * Parse pitch / velocity / RR tokens from a sample filename (with or without extension).
 * middleCIsC4 = true: scientific naming (C4 = MIDI 60, default). false: C3 = MIDI 60
 * (Yamaha / Cubase style), i.e. every note token maps one octave (12 semitones) higher.
 */
FilenameTokens parseFilenameTokens(const std::string& filename, bool middleCIsC4 = true);

/** Note token to MIDI. Accepts C4, c#3, Bb2, etc. C4 = 60 (or C3 = 60 when !middleCIsC4). */
std::optional<int> noteNameToMidi(const std::string& token, bool middleCIsC4 = true);

/** Dynamics / velocity word tables from design doc. */
std::optional<int> velocityWordToValue(const std::string& word);

/** Note name for a MIDI note: C4 = 60 (default) or C3 = 60 when !middleCIsC4. */
std::string midiToNoteName(int midiNote, bool middleCIsC4 = true);

/**
 * Stem with velocity / dynamics / round-robin tokens removed, lowercase, single-spaced
 * ("Snare_rr2_v96.wav" -> "snare"). Velocity layers and RR alternates of one sound share it.
 */
std::string layerGroupName(const FilenameTokens& tokens);

/** Natural ("human") order, case-insensitive: "Tom 2" < "Tom 10". Strict weak ordering. */
bool naturalLess(const std::string& a, const std::string& b);

} // namespace looper
