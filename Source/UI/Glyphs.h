#pragma once

#include <juce_core/juce_core.h>

namespace looper {

/**
 * Non-ASCII UI text, in one place.
 *
 * juce::String (const char*) treats its input as ASCII/Latin-1, so a raw UTF-8 literal such
 * as a middle dot typed directly into the source renders as mojibake (two Latin-1 glyphs),
 * and MSVC without /utf-8 can mangle the bytes even earlier. Rule for Source/UI and
 * Source/Plugin: source files stay pure ASCII; build any non-ASCII text from these helpers
 * (UTF-8 escapes decoded via CharPointer_UTF8). SourceEncodingTests enforces the rule.
 */
namespace glyph {

inline juce::String utf8 (const char* escapedUtf8) { return juce::String (juce::CharPointer_UTF8 (escapedUtf8)); }

inline juce::String middleDot()  { return utf8 ("\xc2\xb7"); }     // U+00B7
inline juce::String dotSep()     { return utf8 (" \xc2\xb7 "); }   // " . " separator
inline juce::String ellipsis()   { return utf8 ("\xe2\x80\xa6"); } // U+2026
inline juce::String emDash()     { return utf8 ("\xe2\x80\x94"); } // U+2014
inline juce::String enDash()     { return utf8 ("\xe2\x80\x93"); } // U+2013
inline juce::String plusMinus()  { return utf8 ("\xc2\xb1"); }     // U+00B1
inline juce::String arrowRight() { return utf8 ("\xe2\x86\x92"); } // U+2192
inline juce::String arrowLeft()  { return utf8 ("\xe2\x86\x90"); } // U+2190
inline juce::String minus()      { return utf8 ("\xe2\x88\x92"); } // U+2212 minus sign

/** "a b" joined with " <sep> " where sep is a glyph, e.g. join (x, emDash(), y). */
inline juce::String spaced (const juce::String& a, const juce::String& sep, const juce::String& b)
{
    return a + " " + sep + " " + b;
}

} // namespace glyph
} // namespace looper
