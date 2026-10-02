// Guards against mojibake regressions in the UI.
//
// juce::String (const char*) decodes its argument as ASCII/Latin-1, so a raw UTF-8 literal
// ("Off <middle dot> 0 ms") shows up as garbage on screen, and MSVC without /utf-8 may mangle
// it at compile time. Rules (see Source/UI/Glyphs.h):
//   1. No raw non-ASCII bytes inside any string or character literal anywhere in Source/.
//   2. Source/UI and Source/Plugin files are pure ASCII (comments included).
//   3. In Source/UI and Source/Plugin, escaped UTF-8 bytes (\x80-\xff) appear only in Glyphs.h,
//      so every non-ASCII glyph goes through glyph::utf8 (CharPointer_UTF8).

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef LOOPER_SOURCE_DIR
#error "LOOPER_SOURCE_DIR must point at the repository's Source/ directory"
#endif

namespace fs = std::filesystem;

static int g_failed = 0;
static int g_passed = 0;

static void check(bool ok, const std::string& what)
{
    if (ok)
        ++g_passed;
    else
    {
        ++g_failed;
        std::cerr << "FAIL: " << what << "\n";
    }
}

struct Literal
{
    int line = 0;
    std::string body; // raw bytes between the quotes (escapes not decoded)
};

/** Minimal C++ lexer: returns string/char literal bodies, skipping comments. */
static std::vector<Literal> extractLiterals(const std::string& src)
{
    std::vector<Literal> out;
    int line = 1;
    size_t i = 0;
    const size_t n = src.size();
    while (i < n)
    {
        const char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '/' && i + 1 < n && src[i + 1] == '/')
        {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
            {
                if (src[i] == '\n') ++line;
                ++i;
            }
            i += 2;
            continue;
        }
        // Raw string literal R"delim( ... )delim"
        if (c == 'R' && i + 1 < n && src[i + 1] == '"'
            && (i == 0 || !(std::isalnum(static_cast<unsigned char>(src[i - 1])) || src[i - 1] == '_')
                || src[i - 1] == '8' || src[i - 1] == 'u' || src[i - 1] == 'U' || src[i - 1] == 'L'))
        {
            size_t j = i + 2;
            std::string delim;
            while (j < n && src[j] != '(') delim += src[j++];
            const std::string close = ")" + delim + "\"";
            const size_t end = src.find(close, j);
            Literal lit;
            lit.line = line;
            lit.body = src.substr(j + 1, (end == std::string::npos ? n : end) - j - 1);
            for (char ch : lit.body) if (ch == '\n') ++line;
            out.push_back(lit);
            i = (end == std::string::npos) ? n : end + close.size();
            continue;
        }
        if (c == '"' || (c == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(src[i - 1]))
                                        && i + 1 < n && std::isxdigit(static_cast<unsigned char>(src[i + 1])))))
        {
            // (the second clause skips C++14 digit separators like 1'000)
            const char quote = c;
            Literal lit;
            lit.line = line;
            ++i;
            while (i < n && src[i] != quote && src[i] != '\n')
            {
                if (src[i] == '\\' && i + 1 < n) { lit.body += src[i]; ++i; }
                lit.body += src[i];
                ++i;
            }
            ++i;
            out.push_back(lit);
            continue;
        }
        ++i;
    }
    return out;
}

static bool hasRawNonAscii(const std::string& s)
{
    for (unsigned char ch : s)
        if (ch >= 0x80)
            return true;
    return false;
}

static bool hasHighByteEscape(const std::string& body)
{
    for (size_t i = 0; i + 2 < body.size(); ++i)
    {
        if (body[i] == '\\')
        {
            if (body[i + 1] == 'x' || body[i + 1] == 'X')
            {
                const char h = static_cast<char>(std::tolower(static_cast<unsigned char>(body[i + 2])));
                if ((h >= '8' && h <= '9') || (h >= 'a' && h <= 'f'))
                    return true;
            }
            ++i; // skip escaped char
        }
    }
    return false;
}

static std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void testLexerSelfCheck()
{
    std::cout << "testLexerSelfCheck\n";
    const std::string src =
        "// \xc2\xb7 comment\n"
        "/* \xe2\x80\x94 block */ auto a = \"plain\";\n"
        "auto b = \"esc \\\" quote \\xc2\\xb7\"; int n = 1'000; char c = 'x';\n"
        "auto d = \"raw \xc2\xb7\";\n";
    const auto lits = extractLiterals(src);
    check(lits.size() == 4, "lexer finds 4 literals (got " + std::to_string(lits.size()) + ")");
    if (lits.size() == 4)
    {
        check(!hasRawNonAscii(lits[0].body) && !hasHighByteEscape(lits[0].body), "plain literal is clean");
        check(hasHighByteEscape(lits[1].body) && !hasRawNonAscii(lits[1].body), "escaped literal detected");
        check(lits[2].body == "x", "char literal");
        check(hasRawNonAscii(lits[3].body) && lits[3].line == 4, "raw non-ASCII literal detected on line 4");
    }
}

static void testSourceTree()
{
    std::cout << "testSourceTree\n";
    const fs::path root(LOOPER_SOURCE_DIR);
    check(fs::is_directory(root), "Source dir exists: " + root.string());
    check(fs::exists(root / "UI" / "Glyphs.h"), "Source/UI/Glyphs.h exists");

    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".cpp" && ext != ".hpp" && ext != ".mm")
            continue;
        ++files;
        const auto rel = fs::relative(entry.path(), root).generic_string();
        const auto src = readFile(entry.path());
        const bool uiOrPlugin = rel.rfind("UI/", 0) == 0 || rel.rfind("Plugin/", 0) == 0;
        const bool isGlyphs = rel == "UI/Glyphs.h";

        // Rule 1
        bool rule1 = true, rule3 = true;
        for (const auto& lit : extractLiterals(src))
        {
            if (hasRawNonAscii(lit.body))
            {
                rule1 = false;
                std::cerr << "  " << rel << ":" << lit.line << ": raw non-ASCII bytes in a literal\n";
            }
            if (uiOrPlugin && !isGlyphs && hasHighByteEscape(lit.body))
            {
                rule3 = false;
                std::cerr << "  " << rel << ":" << lit.line << ": escaped UTF-8 outside Glyphs.h (use glyph::...)\n";
            }
        }
        check(rule1, rel + ": no raw non-ASCII in literals");
        if (uiOrPlugin)
        {
            check(rule3, rel + ": UTF-8 escapes only via Glyphs.h");
            // Rule 2
            int line = 1;
            bool ascii = true;
            for (unsigned char ch : src)
            {
                if (ch == '\n') ++line;
                if (ch >= 0x80)
                {
                    ascii = false;
                    std::cerr << "  " << rel << ":" << line << ": non-ASCII byte\n";
                    break;
                }
            }
            check(ascii, rel + ": pure ASCII");
        }
    }
    check(files >= 30, "scanned the Source tree (" + std::to_string(files) + " files)");
}

int main()
{
    testLexerSelfCheck();
    testSourceTree();
    std::cout << "SourceEncodingTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
