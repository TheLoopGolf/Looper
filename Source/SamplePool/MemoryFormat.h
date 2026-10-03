#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace looper {

/** "640 KB", "4.2 MB", "42 MB", "1.3 GB" (binary units). ASCII only. */
inline std::string formatBytes(size_t bytes)
{
    char buf[32];
    const double kb = static_cast<double>(bytes) / 1024.0;
    const double mb = kb / 1024.0;
    const double gb = mb / 1024.0;
    if (mb < 1.0)
        std::snprintf(buf, sizeof(buf), "%d KB", static_cast<int>(kb + 0.5));
    else if (mb < 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
    else if (gb < 1.0)
        std::snprintf(buf, sizeof(buf), "%d MB", static_cast<int>(mb + 0.5));
    else
        std::snprintf(buf, sizeof(buf), "%.1f GB", gb);
    return buf;
}

/** Visual tone of the main-view memory chip. */
enum class MemoryTone
{
    InRam,      // everything decoded in RAM (brass ball)
    Streaming,  // long samples stream from disk, no dropouts (fairway ball in flight)
    Warning     // streaming had underruns (sand / bunker)
};

struct MemoryChipText
{
    std::string ram;    // "RAM 42 MB"
    std::string state;  // "Streaming" / "In RAM" / "3 dropouts"
    MemoryTone tone = MemoryTone::InRam;
};

/** JUCE-free wording for the chip (the UI joins ram + state with a middle-dot glyph). */
inline MemoryChipText describeMemory(size_t ramBytes, int streamingSamples, uint64_t underruns)
{
    MemoryChipText t;
    t.ram = "RAM " + formatBytes(ramBytes);
    if (streamingSamples > 0 && underruns > 0)
    {
        t.state = underruns == 1 ? std::string("1 dropout") : std::to_string(underruns) + " dropouts";
        t.tone = MemoryTone::Warning;
    }
    else if (streamingSamples > 0)
    {
        t.state = "Streaming";
        t.tone = MemoryTone::Streaming;
    }
    else
    {
        t.state = "In RAM";
        t.tone = MemoryTone::InRam;
    }
    return t;
}

/** "64k frames" (k = 1024). */
inline std::string formatPreloadFrames(int frames)
{
    if (frames >= 1024 && frames % 1024 == 0)
        return std::to_string(frames / 1024) + "k frames";
    return std::to_string(frames) + " frames";
}

} // namespace looper
