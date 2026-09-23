#include "FilenameTokens.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>

namespace looper {
namespace {

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripExtension(const std::string& filename)
{
    const auto slash = filename.find_last_of("/\\");
    const std::string base = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos)
        return base;
    const auto ext = toLower(base.substr(dot + 1));
    if (ext == "wav" || ext == "aiff" || ext == "aif" || ext == "flac")
        return base.substr(0, dot);
    return base;
}

bool isNoiseToken(const std::string& t)
{
    static const std::vector<std::string> noise = {
        "wav", "aiff", "aif", "flac", "sample", "mapped", "normalized",
        "48k", "44k", "96k", "24b", "16b", "32b", "loop"
    };
    return std::find(noise.begin(), noise.end(), t) != noise.end();
}

} // namespace

std::optional<int> noteNameToMidi(const std::string& tokenIn)
{
    const std::string token = toLower(tokenIn);
    // [a-g] optional #/b, then octave -9..11 (design: -?[0-9]|10|11)
    static const std::regex noteRe(R"(^([a-g])([#b]?)(-?(?:10|11|[0-9]))$)");
    std::smatch m;
    if (!std::regex_match(token, m, noteRe))
        return std::nullopt;

    const char letter = m[1].str()[0];
    const std::string accidental = m[2].str();
    int octave = std::stoi(m[3].str());

    int semitone = 0;
    switch (letter)
    {
        case 'c': semitone = 0; break;
        case 'd': semitone = 2; break;
        case 'e': semitone = 4; break;
        case 'f': semitone = 5; break;
        case 'g': semitone = 7; break;
        case 'a': semitone = 9; break;
        case 'b': semitone = 11; break;
        default: return std::nullopt;
    }

    if (accidental == "#")
        ++semitone;
    else if (accidental == "b")
        --semitone;

    // Scientific: MIDI = (octave + 1) * 12 + semitone  → C4 = 60
    const int midi = (octave + 1) * 12 + semitone;
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return midi;
}

std::optional<int> velocityWordToValue(const std::string& wordIn)
{
    const std::string w = toLower(wordIn);

    static const std::unordered_map<std::string, int> dynamics = {
        {"ppp", 16}, {"pp", 32}, {"p", 48}, {"mp", 64},
        {"mf", 80}, {"f", 96}, {"ff", 112}, {"fff", 127}
    };
    if (const auto it = dynamics.find(w); it != dynamics.end())
        return it->second;

    static const std::unordered_map<std::string, int> words = {
        {"soft", 32}, {"med", 64}, {"medium", 64},
        {"hard", 96}, {"loud", 112}
    };
    if (const auto it = words.find(w); it != words.end())
        return it->second;

    return std::nullopt;
}

FilenameTokens parseFilenameTokens(const std::string& filename)
{
    FilenameTokens out;
    out.originalStem = stripExtension(filename);
    out.normalizedStem = toLower(out.originalStem);

    // Replace separators with spaces for word-ish scanning; keep original for regex on underscored form
    std::string searchable = out.normalizedStem;
    for (char& c : searchable)
    {
        if (c == '-' || c == '_' || c == '.' || c == ' ')
            c = ' ';
    }

    // --- Pitch: note names first (prefer over bare numbers) ---
    // Word-boundary note: letter + optional accidental + octave
    {
        static const std::regex noteRe(R"((?:^|[^a-z0-9])([a-g][#b]?-?(?:10|11|[0-9]))(?=[^a-z0-9]|$))",
                                       std::regex::icase);
        std::sregex_iterator it(out.normalizedStem.begin(), out.normalizedStem.end(), noteRe);
        std::sregex_iterator end;
        for (; it != end; ++it)
        {
            const auto midi = noteNameToMidi((*it)[1].str());
            if (midi)
            {
                out.midiNote = midi;
                break;
            }
        }
    }

    // Also try tokens split on non-alnum
    if (!out.midiNote)
    {
        static const std::regex splitter(R"([^a-z0-9#]+)");
        std::sregex_token_iterator ti(out.normalizedStem.begin(), out.normalizedStem.end(), splitter, -1);
        std::sregex_token_iterator tend;
        for (; ti != tend; ++ti)
        {
            const std::string tok = ti->str();
            if (tok.empty() || isNoiseToken(tok))
                continue;
            if (const auto midi = noteNameToMidi(tok))
            {
                out.midiNote = midi;
                break;
            }
        }
    }

    // --- Velocity numeric ---
    {
        static const std::regex velRe(R"((?:velocity|vel|v)[_-]?(\d{1,3}))", std::regex::icase);
        std::smatch m;
        if (std::regex_search(out.normalizedStem, m, velRe))
        {
            const int v = std::stoi(m[1].str());
            if (v >= 1 && v <= 127)
                out.velocityHint = v;
        }
    }

    // Velocity words / dynamics (only if no numeric yet)
    if (!out.velocityHint)
    {
        static const std::regex wordRe(
            R"((?:^|[^a-z0-9])(ppp|pp|mp|mf|fff|ff|soft|medium|med|hard|loud|[pf])(?=[^a-z0-9]|$))",
            std::regex::icase);
        // Note: single p/f must be careful — use longer alts first in alternation (done above)
        std::smatch m;
        std::string probe = " " + out.normalizedStem + " ";
        // Prefer longest dynamics via ordered search
        static const char* ordered[] = {
            "ppp", "fff", "medium", "soft", "hard", "loud", "med",
            "pp", "mp", "mf", "ff", "p", "f"
        };
        for (const char* w : ordered)
        {
            static const std::regex sep(R"([^a-z0-9])");
            // search as whole token
            const std::string pattern = std::string("(?:^|[^a-z0-9])(") + w + ")(?=[^a-z0-9]|$)";
            std::regex re(pattern, std::regex::icase);
            if (std::regex_search(out.normalizedStem, m, re))
            {
                if (const auto v = velocityWordToValue(w))
                {
                    out.velocityHint = v;
                    break;
                }
            }
        }
    }

    // --- Round-robin ---
    {
        static const std::regex rrRe(R"((?:rr|round|alt)[_-]?(\d+))", std::regex::icase);
        std::smatch m;
        if (std::regex_search(out.normalizedStem, m, rrRe))
        {
            const int rr = std::stoi(m[1].str());
            if (rr >= 0)
                out.rrIndex = rr;
        }
    }

    return out;
}


std::string midiToNoteName(int midiNote)
{
    static constexpr const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int n = std::clamp(midiNote, 0, 127);
    const int pc = n % 12;
    const int oct = (n / 12) - 1; // C4 = 60 → octave 4
    return std::string(kNames[pc]) + std::to_string(oct);
}

} // namespace looper
