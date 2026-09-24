#include "PatchStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace looper {
namespace {

// ---------------------------------------------------------------------------
// Tiny deterministic JSON helpers (schema-specific; not a full JSON library)
// ---------------------------------------------------------------------------

std::string escapeJsonString(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
                break;
        }
    }
    return out;
}

std::string quote(const std::string& s) { return "\"" + escapeJsonString(s) + "\""; }

std::string formatNumber(double v)
{
    if (!std::isfinite(v))
        return "0";
    std::ostringstream oss;
    oss.precision(15);
    oss << std::noshowpoint << v;
    auto s = oss.str();
    // Prefer integer form when exact
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
        && s.find('E') == std::string::npos)
        return s;
    return s;
}

std::string formatInt(int64_t v) { return std::to_string(v); }

class JsonWriter
{
public:
    explicit JsonWriter(std::string& out) : out_(out) {}

    void beginObject()
    {
        prepValue();
        out_ += '{';
        stack_.push_back(true);
    }

    void endObject()
    {
        out_ += '}';
        if (!stack_.empty()) stack_.pop_back();
        markWritten();
    }

    void beginArray()
    {
        prepValue();
        out_ += '[';
        stack_.push_back(true);
    }

    void endArray()
    {
        out_ += ']';
        if (!stack_.empty()) stack_.pop_back();
        markWritten();
    }

    void key(const char* k)
    {
        // Keys are only valid inside objects; emit comma between members.
        if (!stack_.empty() && !stack_.back())
            out_ += ',';
        if (!stack_.empty())
            stack_.back() = false;
        out_ += quote(k);
        out_ += ':';
        expectingValue_ = true;
    }

    void nullVal() { emitScalar("null"); }
    void boolVal(bool v) { emitScalar(v ? "true" : "false"); }
    void numberVal(double v) { emitScalar(formatNumber(v)); }
    void intVal(int64_t v) { emitScalar(formatInt(v)); }
    void stringVal(const std::string& s) { emitScalar(quote(s)); }
    void raw(const std::string& s) { emitScalar(s); }

private:
    void prepValue()
    {
        if (expectingValue_)
        {
            expectingValue_ = false;
            return;
        }
        if (!stack_.empty())
        {
            if (!stack_.back())
                out_ += ',';
            stack_.back() = false;
        }
    }

    void emitScalar(const std::string& s)
    {
        prepValue();
        out_ += s;
        markWritten();
    }

    void markWritten()
    {
        expectingValue_ = false;
        if (!stack_.empty())
            stack_.back() = false;
    }

    std::string& out_;
    std::vector<bool> stack_; // true = container still empty (no member yet)
    bool expectingValue_ = false;
};

// Minimal JSON reader -------------------------------------------------------

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JsonValue> a;
    std::vector<std::pair<std::string, JsonValue>> o;

    const JsonValue* find(const char* key) const
    {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : o)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }

    std::optional<std::string> asString() const
    {
        if (type == Type::String) return s;
        return std::nullopt;
    }

    std::optional<double> asNumber() const
    {
        if (type == Type::Number) return n;
        return std::nullopt;
    }

    std::optional<int> asInt() const
    {
        if (type != Type::Number) return std::nullopt;
        return static_cast<int>(std::llround(n));
    }

    std::optional<int64_t> asInt64() const
    {
        if (type != Type::Number) return std::nullopt;
        return static_cast<int64_t>(std::llround(n));
    }

    std::optional<bool> asBool() const
    {
        if (type == Type::Bool) return b;
        return std::nullopt;
    }
};

class JsonParser
{
public:
    explicit JsonParser(const std::string& src) : src_(src) {}

    std::optional<JsonValue> parse()
    {
        skipWs();
        auto v = parseValue();
        if (!v) return std::nullopt;
        skipWs();
        if (pos_ != src_.size()) return std::nullopt;
        return v;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;

    void skipWs()
    {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
    }

    bool match(char c)
    {
        skipWs();
        if (pos_ < src_.size() && src_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<JsonValue> parseValue()
    {
        skipWs();
        if (pos_ >= src_.size()) return std::nullopt;
        const char c = src_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull()
    {
        if (src_.compare(pos_, 4, "null") != 0) return std::nullopt;
        pos_ += 4;
        JsonValue v;
        v.type = JsonValue::Type::Null;
        return v;
    }

    std::optional<JsonValue> parseBool()
    {
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        if (src_.compare(pos_, 4, "true") == 0)
        {
            pos_ += 4;
            v.b = true;
            return v;
        }
        if (src_.compare(pos_, 5, "false") == 0)
        {
            pos_ += 5;
            v.b = false;
            return v;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber()
    {
        const size_t start = pos_;
        if (pos_ < src_.size() && src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_])))
            return std::nullopt;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_])))
                return std::nullopt;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])))
                ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_])))
                return std::nullopt;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])))
                ++pos_;
        }
        JsonValue v;
        v.type = JsonValue::Type::Number;
        try
        {
            v.n = std::stod(src_.substr(start, pos_ - start));
        }
        catch (...)
        {
            return std::nullopt;
        }
        return v;
    }

    std::optional<JsonValue> parseString()
    {
        if (!match('"')) return std::nullopt;
        std::string s;
        while (pos_ < src_.size())
        {
            char c = src_[pos_++];
            if (c == '"')
            {
                JsonValue v;
                v.type = JsonValue::Type::String;
                v.s = std::move(s);
                return v;
            }
            if (c == '\\')
            {
                if (pos_ >= src_.size()) return std::nullopt;
                char e = src_[pos_++];
                switch (e)
                {
                    case '"': case '\\': case '/': s += e; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u':
                    {
                        if (pos_ + 4 > src_.size()) return std::nullopt;
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            char h = src_[pos_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else return std::nullopt;
                        }
                        if (code < 0x80)
                            s += static_cast<char>(code);
                        else if (code < 0x800)
                        {
                            s += static_cast<char>(0xC0 | (code >> 6));
                            s += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        else
                        {
                            s += static_cast<char>(0xE0 | (code >> 12));
                            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            s += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return std::nullopt;
                }
            }
            else if (static_cast<unsigned char>(c) < 0x20)
                return std::nullopt;
            else
                s += c;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseArray()
    {
        if (!match('[')) return std::nullopt;
        JsonValue v;
        v.type = JsonValue::Type::Array;
        skipWs();
        if (match(']')) return v;
        while (true)
        {
            auto item = parseValue();
            if (!item) return std::nullopt;
            v.a.push_back(std::move(*item));
            skipWs();
            if (match(']')) return v;
            if (!match(',')) return std::nullopt;
        }
    }

    std::optional<JsonValue> parseObject()
    {
        if (!match('{')) return std::nullopt;
        JsonValue v;
        v.type = JsonValue::Type::Object;
        skipWs();
        if (match('}')) return v;
        while (true)
        {
            auto key = parseString();
            if (!key || key->type != JsonValue::Type::String) return std::nullopt;
            if (!match(':')) return std::nullopt;
            auto val = parseValue();
            if (!val) return std::nullopt;
            v.o.emplace_back(std::move(key->s), std::move(*val));
            skipWs();
            if (match('}')) return v;
            if (!match(',')) return std::nullopt;
        }
    }
};

void writeOptionalInt64(JsonWriter& w, const char* key, const std::optional<int64_t>& v)
{
    if (!v) return;
    w.key(key);
    w.intVal(*v);
}

void writeOptionalDouble(JsonWriter& w, const char* key, const std::optional<double>& v)
{
    if (!v) return;
    w.key(key);
    w.numberVal(*v);
}

void writeOptionalInt(JsonWriter& w, const char* key, const std::optional<int>& v)
{
    if (!v) return;
    w.key(key);
    w.intVal(*v);
}

void writeOptionalString(JsonWriter& w, const char* key, const std::optional<std::string>& v)
{
    if (!v) return;
    w.key(key);
    w.stringVal(*v);
}

void writeSample(JsonWriter& w, const SampleRef& s)
{
    w.beginObject();
    w.key("id");
    w.stringVal(s.id);
    w.key("path");
    w.stringVal(s.path);
    w.key("displayName");
    w.stringVal(s.displayName);
    writeOptionalString(w, "fileHash", s.fileHash);
    writeOptionalInt64(w, "durationSamples", s.durationSamples);
    writeOptionalDouble(w, "sampleRate", s.sampleRate);
    writeOptionalInt(w, "channels", s.channels);
    writeOptionalDouble(w, "detectedPitchHz", s.detectedPitchHz);
    writeOptionalInt(w, "detectedRootKey", s.detectedRootKey);
    writeOptionalInt64(w, "loopStart", s.loopStart);
    writeOptionalInt64(w, "loopEnd", s.loopEnd);
    w.endObject();
}

void writeZone(JsonWriter& w, const Zone& z)
{
    w.beginObject();
    w.key("sampleId");
    w.stringVal(z.sampleId);
    w.key("rootKey");
    w.intVal(z.rootKey);
    w.key("keyLow");
    w.intVal(z.keyLow);
    w.key("keyHigh");
    w.intVal(z.keyHigh);
    w.key("velLow");
    w.intVal(z.velLow);
    w.key("velHigh");
    w.intVal(z.velHigh);
    w.key("rrGroup");
    w.intVal(z.rrGroup);
    w.key("rrIndex");
    w.intVal(z.rrIndex);
    w.key("tuneCents");
    w.numberVal(z.tuneCents);
    w.key("coarseTranspose");
    w.intVal(z.coarseTranspose);
    w.key("gainDb");
    w.numberVal(z.gainDb);
    w.key("pan");
    w.numberVal(z.pan);
    writeOptionalInt64(w, "sampleStart", z.sampleStart);
    writeOptionalInt64(w, "sampleEnd", z.sampleEnd);
    w.endObject();
}

void writeMap(JsonWriter& w, const InstrumentMap& map)
{
    w.beginObject();
    w.key("volumeDb");
    w.numberVal(map.volumeDb);
    w.key("polyphonyLimit");
    w.intVal(map.polyphonyLimit);
    w.key("glideMs");
    w.numberVal(map.glideMs);
    w.key("velCurve");
    w.stringVal(PatchStore::velCurveToString(map.velCurve));
    w.key("modWheelTarget");
    w.stringVal(PatchStore::modWheelTargetToString(map.modWheelTarget));
    w.key("zones");
    w.beginArray();
    for (const auto& z : map.zones)
        writeZone(w, z);
    w.endArray();
    w.endObject();
}

SampleRef readSample(const JsonValue& v)
{
    SampleRef s;
    if (auto* id = v.find("id")) if (auto x = id->asString()) s.id = *x;
    if (auto* p = v.find("path")) if (auto x = p->asString()) s.path = *x;
    if (auto* d = v.find("displayName")) if (auto x = d->asString()) s.displayName = *x;
    if (auto* h = v.find("fileHash")) if (auto x = h->asString()) s.fileHash = *x;
    if (auto* x = v.find("durationSamples")) if (auto n = x->asInt64()) s.durationSamples = *n;
    if (auto* x = v.find("sampleRate")) if (auto n = x->asNumber()) s.sampleRate = *n;
    if (auto* x = v.find("channels")) if (auto n = x->asInt()) s.channels = *n;
    if (auto* x = v.find("detectedPitchHz")) if (auto n = x->asNumber()) s.detectedPitchHz = *n;
    if (auto* x = v.find("detectedRootKey")) if (auto n = x->asInt()) s.detectedRootKey = *n;
    if (auto* x = v.find("loopStart")) if (auto n = x->asInt64()) s.loopStart = *n;
    if (auto* x = v.find("loopEnd")) if (auto n = x->asInt64()) s.loopEnd = *n;
    return s;
}

Zone readZone(const JsonValue& v)
{
    Zone z;
    if (auto* id = v.find("sampleId")) if (auto x = id->asString()) z.sampleId = *x;
    if (auto* x = v.find("rootKey")) if (auto n = x->asInt()) z.rootKey = *n;
    if (auto* x = v.find("keyLow")) if (auto n = x->asInt()) z.keyLow = *n;
    if (auto* x = v.find("keyHigh")) if (auto n = x->asInt()) z.keyHigh = *n;
    if (auto* x = v.find("velLow")) if (auto n = x->asInt()) z.velLow = *n;
    if (auto* x = v.find("velHigh")) if (auto n = x->asInt()) z.velHigh = *n;
    if (auto* x = v.find("rrGroup")) if (auto n = x->asInt()) z.rrGroup = *n;
    if (auto* x = v.find("rrIndex")) if (auto n = x->asInt()) z.rrIndex = *n;
    if (auto* x = v.find("tuneCents")) if (auto n = x->asNumber()) z.tuneCents = static_cast<float>(*n);
    if (auto* x = v.find("coarseTranspose")) if (auto n = x->asInt()) z.coarseTranspose = *n;
    if (auto* x = v.find("gainDb")) if (auto n = x->asNumber()) z.gainDb = static_cast<float>(*n);
    if (auto* x = v.find("pan")) if (auto n = x->asNumber()) z.pan = static_cast<float>(*n);
    if (auto* x = v.find("sampleStart")) if (auto n = x->asInt64()) z.sampleStart = *n;
    if (auto* x = v.find("sampleEnd")) if (auto n = x->asInt64()) z.sampleEnd = *n;
    return z;
}

InstrumentMap readMap(const JsonValue& v)
{
    InstrumentMap map;
    if (auto* x = v.find("volumeDb")) if (auto n = x->asNumber()) map.volumeDb = static_cast<float>(*n);
    if (auto* x = v.find("polyphonyLimit")) if (auto n = x->asInt()) map.polyphonyLimit = *n;
    if (auto* x = v.find("glideMs")) if (auto n = x->asNumber()) map.glideMs = static_cast<float>(*n);
    if (auto* x = v.find("velCurve"))
        if (auto s = x->asString())
            if (auto c = PatchStore::velCurveFromString(*s))
                map.velCurve = *c;
    if (auto* x = v.find("modWheelTarget"))
        if (auto s = x->asString())
            if (auto t = PatchStore::modWheelTargetFromString(*s))
                map.modWheelTarget = *t;
    if (auto* zones = v.find("zones"); zones && zones->type == JsonValue::Type::Array)
        for (const auto& zv : zones->a)
            map.zones.push_back(readZone(zv));
    return map;
}

std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path)
    {
        if (c == '/' || c == '\\')
        {
            if (!cur.empty())
            {
                parts.push_back(cur);
                cur.clear();
            }
        }
        else
            cur += c;
    }
    if (!cur.empty())
        parts.push_back(cur);
    return parts;
}

std::string joinPath(const std::vector<std::string>& parts, bool absolute)
{
    std::string out;
    if (absolute)
        out = "/";
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i) out += '/';
        out += parts[i];
    }
    if (out.empty())
        return absolute ? "/" : ".";
    return out;
}

std::vector<std::string> collapseParts(const std::vector<std::string>& in)
{
    std::vector<std::string> out;
    for (const auto& p : in)
    {
        if (p == "." || p.empty())
            continue;
        if (p == "..")
        {
            if (!out.empty() && out.back() != "..")
                out.pop_back();
            else
                out.push_back("..");
            continue;
        }
        out.push_back(p);
    }
    return out;
}

} // namespace

std::string PatchStore::velCurveToString(VelCurve c)
{
    switch (c)
    {
        case VelCurve::Soft: return "soft";
        case VelCurve::Hard: return "hard";
        default: return "linear";
    }
}

std::optional<VelCurve> PatchStore::velCurveFromString(const std::string& s)
{
    if (s == "linear") return VelCurve::Linear;
    if (s == "soft") return VelCurve::Soft;
    if (s == "hard") return VelCurve::Hard;
    return std::nullopt;
}

std::string PatchStore::modWheelTargetToString(ModWheelTarget t)
{
    switch (t)
    {
        case ModWheelTarget::Volume: return "Volume";
        default: return "FilterCutoff";
    }
}

std::optional<ModWheelTarget> PatchStore::modWheelTargetFromString(const std::string& s)
{
    if (s == "FilterCutoff" || s == "filterCutoff") return ModWheelTarget::FilterCutoff;
    if (s == "Volume" || s == "volume") return ModWheelTarget::Volume;
    return std::nullopt;
}

bool PatchStore::isAbsolutePath(const std::string& path)
{
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    // Windows drive: C:\ or C:/
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        return true;
    // UNC \\server
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return true;
    return false;
}

std::string PatchStore::normalizePath(const std::string& path)
{
    if (path.empty()) return path;
    const bool abs = isAbsolutePath(path);
    // Preserve Windows drive prefix
    std::string drive;
    std::string rest = path;
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
    {
        drive = path.substr(0, 2);
        rest = path.substr(2);
    }
    auto parts = collapseParts(splitPath(rest));
    std::string joined = joinPath(parts, abs && drive.empty());
    if (!drive.empty())
    {
        if (joined == ".") return drive + "/";
        if (!joined.empty() && joined[0] != '/')
            return drive + "/" + joined;
        return drive + joined;
    }
    return joined;
}

std::string PatchStore::parentDirectory(const std::string& filePath)
{
    if (filePath.empty()) return {};
    auto n = normalizePath(filePath);
    const auto slash = n.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    // Windows drive root C:/
    if (slash == 2 && n.size() >= 2 && n[1] == ':')
        return n.substr(0, 3);
    return n.substr(0, slash);
}

std::string PatchStore::makeRelativeTo(const std::string& path, const std::string& baseDir)
{
    if (path.empty()) return path;
    if (!isAbsolutePath(path) || !isAbsolutePath(baseDir))
        return normalizePath(path);

    auto pParts = collapseParts(splitPath(normalizePath(path)));
    auto bParts = collapseParts(splitPath(normalizePath(baseDir)));

    // Different Windows drives → keep absolute
    if (!path.empty() && !baseDir.empty()
        && std::isalpha(static_cast<unsigned char>(path[0]))
        && std::isalpha(static_cast<unsigned char>(baseDir[0]))
        && path[1] == ':' && baseDir[1] == ':'
        && std::tolower(static_cast<unsigned char>(path[0]))
               != std::tolower(static_cast<unsigned char>(baseDir[0])))
        return normalizePath(path);

    size_t i = 0;
    while (i < pParts.size() && i < bParts.size() && pParts[i] == bParts[i])
        ++i;

    std::vector<std::string> rel;
    for (size_t j = i; j < bParts.size(); ++j)
        rel.push_back("..");
    for (size_t j = i; j < pParts.size(); ++j)
        rel.push_back(pParts[j]);

    if (rel.empty())
        return ".";
    return joinPath(rel, false);
}

std::string PatchStore::resolveAgainst(const std::string& path, const std::string& baseDir)
{
    if (path.empty()) return path;
    if (isAbsolutePath(path))
        return normalizePath(path);
    if (baseDir.empty())
        return normalizePath(path);

    const bool abs = isAbsolutePath(baseDir);
    std::string drive;
    auto base = normalizePath(baseDir);
    // Strip a Windows drive prefix before splitting so it is not duplicated
    // (otherwise "C:/dir" + "a.wav" became "C:/C:/dir/a.wav").
    if (base.size() >= 2 && std::isalpha(static_cast<unsigned char>(base[0])) && base[1] == ':')
    {
        drive = base.substr(0, 2);
        base = base.substr(2);
    }
    auto parts = splitPath(base);
    auto more = splitPath(path);
    parts.insert(parts.end(), more.begin(), more.end());
    parts = collapseParts(parts);

    if (!drive.empty())
    {
        auto joined = joinPath(parts, false);
        return drive + "/" + joined;
    }
    return joinPath(parts, abs);
}

Patch PatchStore::withRelativeSamplePaths(Patch patch, const std::string& patchDir)
{
    for (auto& s : patch.samples)
    {
        if (s.path.empty()) continue;
        // Keep special demo URI as-is
        if (s.path.find("://") != std::string::npos) continue;
        s.path = makeRelativeTo(s.path, patchDir);
    }
    patch.patchRoot = ".";
    return patch;
}

void PatchStore::resolveSamplePaths(Patch& patch, const std::string& patchDir)
{
    for (auto& s : patch.samples)
    {
        if (s.path.empty()) continue;
        if (s.path.find("://") != std::string::npos) continue;
        s.path = resolveAgainst(s.path, patchDir);
    }
}

std::string PatchStore::toJson(const Patch& patch)
{
    std::string out;
    JsonWriter w(out);
    w.beginObject();
    w.key("schemaVersion");
    w.intVal(patch.schemaVersion);
    w.key("name");
    w.stringVal(patch.name);
    w.key("patchRoot");
    w.stringVal(patch.patchRoot.empty() ? "." : patch.patchRoot);

    w.key("samples");
    w.beginArray();
    for (const auto& s : patch.samples)
        writeSample(w, s);
    w.endArray();

    w.key("map");
    writeMap(w, patch.map);

    // Deterministic param order by sorting keys
    if (!patch.params.empty())
    {
        std::vector<std::string> keys;
        keys.reserve(patch.params.size());
        for (const auto& kv : patch.params)
            keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        w.key("params");
        w.beginObject();
        for (const auto& k : keys)
        {
            w.key(k.c_str());
            w.numberVal(patch.params.at(k));
        }
        w.endObject();
    }

    w.endObject();
    return out;
}

std::optional<Patch> PatchStore::fromJson(const std::string& json)
{
    JsonParser parser(json);
    auto root = parser.parse();
    if (!root || root->type != JsonValue::Type::Object)
        return std::nullopt;

    Patch patch;
    if (auto* sv = root->find("schemaVersion"))
    {
        if (auto n = sv->asInt())
            patch.schemaVersion = *n;
    }
    if (patch.schemaVersion != kSchemaVersion && patch.schemaVersion != 1)
        return std::nullopt; // unknown future schema

    if (auto* n = root->find("name")) if (auto s = n->asString()) patch.name = *s;
    if (auto* n = root->find("patchRoot")) if (auto s = n->asString()) patch.patchRoot = *s;

    if (auto* samples = root->find("samples"); samples && samples->type == JsonValue::Type::Array)
        for (const auto& sv : samples->a)
            patch.samples.push_back(readSample(sv));

    if (auto* map = root->find("map"))
        patch.map = readMap(*map);

    if (auto* params = root->find("params"); params && params->type == JsonValue::Type::Object)
        for (const auto& kv : params->o)
            if (auto n = kv.second.asNumber())
                patch.params[kv.first] = *n;

    return patch;
}

bool PatchStore::save(const std::string& jsonPath, const Patch& patch)
{
    const auto dir = parentDirectory(jsonPath);
    auto toWrite = withRelativeSamplePaths(patch, dir);
    toWrite.schemaVersion = kSchemaVersion;
    const auto text = toJson(toWrite);

    std::ofstream ofs(jsonPath, std::ios::binary | std::ios::trunc);
    if (!ofs)
        return false;
    ofs << text;
    return static_cast<bool>(ofs);
}

std::optional<PatchLoadResult> PatchStore::load(const std::string& jsonPath)
{
    std::ifstream ifs(jsonPath, std::ios::binary);
    if (!ifs)
        return std::nullopt;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    auto parsed = fromJson(ss.str());
    if (!parsed)
        return std::nullopt;

    PatchLoadResult result;
    result.patch = std::move(*parsed);
    const auto dir = parentDirectory(jsonPath);
    resolveSamplePaths(result.patch, dir);

    for (const auto& s : result.patch.samples)
    {
        if (s.path.empty() || s.path.find("://") != std::string::npos)
            continue;
        std::ifstream probe(s.path, std::ios::binary);
        if (!probe)
        {
            result.missingSamplePaths.push_back(s.path);
            result.offlineSampleIds.push_back(s.id);
        }
    }
    return result;
}

} // namespace looper
