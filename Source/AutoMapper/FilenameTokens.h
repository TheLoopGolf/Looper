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

/** Parse pitch / velocity / RR tokens from a sample filename (with or without extension). */
FilenameTokens parseFilenameTokens(const std::string& filename);

/** Scientific pitch: C4 = MIDI 60. Accepts C4, c#3, Bb2, etc. */
std::optional<int> noteNameToMidi(const std::string& token);

/** Dynamics / velocity word tables from design doc. */
std::optional<int> velocityWordToValue(const std::string& word);

/** Scientific pitch name for MIDI note (C4 = 60). */
std::string midiToNoteName(int midiNote);

} // namespace looper
