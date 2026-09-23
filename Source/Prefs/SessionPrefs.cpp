#include "SessionPrefs.h"
#include "../PatchStore/PatchStore.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace looper {
namespace {

std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

/** Very small helper: find `"key":` then parse bool / number / quoted string. */
std::optional<std::string> findStringField(const std::string& json, const std::string& key)
{
    const std::string pat = "\"" + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        ++pos;
    if (pos >= json.size()) return std::nullopt;
    if (json[pos] == '"')
    {
        ++pos;
        std::string out;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                out.push_back(json[pos + 1]);
                pos += 2;
                continue;
            }
            out.push_back(json[pos++]);
        }
        return out;
    }
    // bare token until comma / } / whitespace end
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n')
        ++end;
    auto tok = json.substr(pos, end - pos);
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t'))
        tok.pop_back();
    return tok;
}

std::optional<double> findNumberField(const std::string& json, const std::string& key)
{
    auto tok = findStringField(json, key);
    if (!tok) return std::nullopt;
    try { return std::stod(*tok); }
    catch (...) { return std::nullopt; }
}

std::optional<bool> findBoolField(const std::string& json, const std::string& key)
{
    auto tok = findStringField(json, key);
    if (!tok) return std::nullopt;
    if (*tok == "true") return true;
    if (*tok == "false") return false;
    return std::nullopt;
}

} // namespace

std::string SessionPrefs::toJson() const
{
    std::ostringstream o;
    o << '{'
      << "\"polyphony\":" << polyphony << ','
      << "\"glideMs\":" << glideMs << ','
      << "\"masterSoftClip\":" << (masterSoftClip ? "true" : "false") << ','
      << "\"defaultFilterType\":" << defaultFilterType << ','
      << "\"middleCIsC4\":" << (middleCIsC4 ? "true" : "false") << ','
      << "\"preferFullKeyboardSpan\":" << (preferFullKeyboardSpan ? "true" : "false") << ','
      << "\"cycleRrDefault\":" << (cycleRrDefault ? "true" : "false") << ','
      << "\"velCurve\":\"" << escape(PatchStore::velCurveToString(velCurve)) << "\","
      << "\"openReviewAfterImport\":" << (openReviewAfterImport ? "true" : "false") << ','
      << "\"pitchBendRangeSemis\":" << pitchBendRangeSemis << ','
      << "\"modWheelTarget\":\"" << escape(PatchStore::modWheelTargetToString(modWheelTarget)) << "\""
      << '}';
    return o.str();
}

std::optional<SessionPrefs> SessionPrefs::fromJson(const std::string& json)
{
    if (json.empty()) return std::nullopt;
    SessionPrefs p;
    if (auto n = findNumberField(json, "polyphony"))
        p.polyphony = std::clamp(static_cast<int>(std::lround(*n)), 1, 128);
    if (auto n = findNumberField(json, "glideMs"))
        p.glideMs = static_cast<float>(std::max(0.0, *n));
    if (auto b = findBoolField(json, "masterSoftClip"))
        p.masterSoftClip = *b;
    if (auto n = findNumberField(json, "defaultFilterType"))
        p.defaultFilterType = std::clamp(static_cast<int>(std::lround(*n)), 0, 2);
    if (auto b = findBoolField(json, "middleCIsC4"))
        p.middleCIsC4 = *b;
    if (auto b = findBoolField(json, "preferFullKeyboardSpan"))
        p.preferFullKeyboardSpan = *b;
    if (auto b = findBoolField(json, "cycleRrDefault"))
        p.cycleRrDefault = *b;
    if (auto s = findStringField(json, "velCurve"))
        if (auto c = PatchStore::velCurveFromString(*s))
            p.velCurve = *c;
    if (auto b = findBoolField(json, "openReviewAfterImport"))
        p.openReviewAfterImport = *b;
    if (auto n = findNumberField(json, "pitchBendRangeSemis"))
        p.pitchBendRangeSemis = static_cast<float>(std::max(0.0, *n));
    if (auto s = findStringField(json, "modWheelTarget"))
        if (auto t = PatchStore::modWheelTargetFromString(*s))
            p.modWheelTarget = *t;
    return p;
}

} // namespace looper
