#include "../Source/Prefs/SessionPrefs.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace looper;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    const auto _a = (a); const auto _b = (b); \
    if (!(_a == _b)) { \
        std::cerr << "FAIL: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    if (std::fabs(static_cast<double>(a) - static_cast<double>(b)) > (eps)) { \
        std::cerr << "FAIL: near " << #a << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static void testDefaults()
{
    SessionPrefs p;
    CHECK_EQ(p.polyphony, 64);
    CHECK_NEAR(p.glideMs, 0.0f, 1e-6);
    CHECK(!p.masterSoftClip);
    CHECK_EQ(p.defaultFilterType, 0);
    CHECK(p.middleCIsC4);
    CHECK(p.preferFullKeyboardSpan);
    CHECK(p.cycleRrDefault);
    CHECK(p.velCurve == VelCurve::Linear);
    CHECK(p.openReviewAfterImport);
    CHECK_NEAR(p.pitchBendRangeSemis, 2.0f, 1e-6);
    CHECK(p.modWheelTarget == ModWheelTarget::FilterCutoff);

    auto opt = p.toAutoMapOptions();
    CHECK(opt.middleCIsC4);
    CHECK(opt.preferFullKeyboardSpan);
    CHECK(opt.cycleRrDefault);
}

static void testRoundTrip()
{
    SessionPrefs p;
    p.polyphony = 32;
    p.glideMs = 50.0f;
    p.masterSoftClip = true;
    p.defaultFilterType = 2;
    p.preferFullKeyboardSpan = false;
    p.cycleRrDefault = true;
    p.velCurve = VelCurve::Hard;
    p.openReviewAfterImport = false;
    p.pitchBendRangeSemis = 2.0f;
    p.modWheelTarget = ModWheelTarget::Volume;

    const auto json = p.toJson();
    CHECK(json.find("\"polyphony\":32") != std::string::npos);
    CHECK(json.find("\"masterSoftClip\":true") != std::string::npos);
    CHECK(json.find("\"velCurve\":\"hard\"") != std::string::npos);
    CHECK(json.find("\"openReviewAfterImport\":false") != std::string::npos);

    auto loaded = SessionPrefs::fromJson(json);
    CHECK(loaded.has_value());
    CHECK(loaded->approximatelyEqual(p));
}

static void testPartialJson()
{
    auto loaded = SessionPrefs::fromJson(R"({"polyphony":96,"velCurve":"soft"})");
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->polyphony, 96);
    CHECK(loaded->velCurve == VelCurve::Soft);
    CHECK(loaded->openReviewAfterImport); // default retained
}

static void testClampPolyphony()
{
    auto loaded = SessionPrefs::fromJson(R"({"polyphony":999})");
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->polyphony, 128);
}

int main()
{
    testDefaults();
    testRoundTrip();
    testPartialJson();
    testClampPolyphony();
    std::cout << "SessionPrefsTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
