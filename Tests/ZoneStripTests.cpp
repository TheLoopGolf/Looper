// Zone strip / multi-select tests: keyboard-strip geometry, key snapping and hit-testing,
// drag maths (edges, root, body move, Alt-draw) with clamping and no-inversion rules,
// multi-selection rules, mixed values, relative / absolute group edits, all-or-nothing group
// edit records, saved-state ("unsaved") tracking through undo / redo, zone-gain ramp on
// sounding notes (no step discontinuity, no allocation), exact-zone audition (bypasses
// overlaps and round-robin) and, when built with JUCE, one-undo-step grouping on the real
// juce::UndoManager.

#include "../Source/SamplePool/SamplePool.h"
#include "../Source/VoiceEngine/GainRamp.h"
#include "../Source/VoiceEngine/VoiceEngine.h"
#include "../Source/ZoneEdit/EditHistory.h"
#include "../Source/ZoneEdit/ZoneEditor.h"
#include "../Source/ZoneEdit/ZoneSelection.h"
#include "../Source/ZoneEdit/ZoneStrip.h"

#if LOOPER_ZONE_EDIT_WITH_JUCE
 #include "../Source/ZoneEdit/ZoneEditAction.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace looper;

#if defined(__GNUC__) && !defined(__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// --- Global allocation counter (audio-thread paths must not allocate or free) ---------------
static std::atomic<long> g_allocs { 0 };
static std::atomic<long> g_frees { 0 };
void* operator new(std::size_t n)
{
    ++g_allocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    ++g_allocs;
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete[](void* p) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) ++g_frees; std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) ++g_frees; std::free(p); }

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
        std::cerr << "FAIL: " << #a << " == " << #b << " (" << _a << " vs " << _b \
                  << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    const double _a = static_cast<double>(a); const double _b = static_cast<double>(b); \
    if (!(std::fabs(_a - _b) <= (eps))) { \
        std::cerr << "FAIL: " << #a << " ~= " << #b << " (" << _a << " vs " << _b \
                  << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } else { ++g_passed; } \
} while (0)

static Zone makeZone(const std::string& id, int keyLo, int keyHi, int root,
                     int velLo = 1, int velHi = 127, int rrGroup = 0, int rrIndex = 0)
{
    Zone z;
    z.sampleId = id;
    z.rootKey = root;
    z.keyLow = keyLo;
    z.keyHigh = keyHi;
    z.velLow = velLo;
    z.velHigh = velHi;
    z.rrGroup = rrGroup;
    z.rrIndex = rrIndex;
    return z;
}

static std::vector<ZoneKeySpan> spansOf(const InstrumentMap& m)
{
    std::vector<ZoneKeySpan> out;
    for (const auto& z : m.zones)
        out.push_back({ z.keyLow, z.keyHigh, z.rootKey });
    return out;
}

static const Zone* changed(const std::vector<ZoneChange>& c, size_t index)
{
    for (const auto& x : c)
        if (x.index == index)
            return &x.zone;
    return nullptr;
}

static ZoneSelection sel(std::vector<size_t> idx, int primary)
{
    ZoneSelection s;
    s.set(std::move(idx), primary);
    return s;
}

/** 48 (C3) .. 72 (C5): 15 white keys, 10 px each, key area 60 px tall at (0, 0). */
static KeyStripLayout testLayout()
{
    KeyStripLayout L;
    L.lowKey = 48;
    L.highKey = 72;
    L.x = 0.0f;
    L.y = 0.0f;
    L.width = 150.0f;
    L.height = 60.0f;
    return L;
}

// ---------------------------------------------------------------------------------------

static void testStripGeometryAndSnapping()
{
    std::cout << "testStripGeometryAndSnapping\n";
    const auto L = testLayout();
    CHECK_EQ(L.whiteCount(), 15);
    CHECK_NEAR(L.whiteWidth(), 10.0, 1e-6);
    CHECK_NEAR(L.keyLeft(48), 0.0, 1e-6);           // C3
    CHECK_NEAR(L.keyLeft(49), 6.5, 1e-5);           // C#3 straddles C/D
    CHECK_NEAR(L.keyLeft(50), 10.0, 1e-6);          // D3
    CHECK_NEAR(L.keyLeft(60), 70.0, 1e-5);          // C4: 7 white keys in
    CHECK_NEAR(L.keyLeft(47), -10.0, 1e-5);         // off-screen keys still have positions
    CHECK_NEAR(L.keyCentre(48), 4.5, 1e-5);

    // Snapping: nearest key centre, chromatic
    CHECK_EQ(L.nearestKey(3.0f), 48);
    CHECK_EQ(L.nearestKey(9.0f), 49);
    CHECK_EQ(L.nearestKey(13.5f), 50);
    CHECK_EQ(L.nearestKey(74.0f), 60);
    CHECK_EQ(L.nearestKey(-40.0f), 48);              // left of the strip -> lowest visible key
    CHECK_EQ(L.nearestKey(900.0f), 72);              // right of the strip -> highest visible key
    for (int m = 48; m <= 72; ++m)
        CHECK_EQ(L.nearestKey(L.keyCentre(m)), m);  // every key's centre snaps to itself

    // Hit test: black keys on top in the upper part, white keys below
    CHECK_EQ(L.keyAt(8.0f, 10.0f), 49);
    CHECK_EQ(L.keyAt(8.0f, 50.0f), 48);
    CHECK_EQ(L.keyAt(-1.0f, 10.0f), -1);
    CHECK_EQ(L.keyAt(10.0f, 70.0f), -1);

    // Spans
    auto s = L.spanFor(50, 52);                      // D3..E3
    CHECK(s.visible);
    CHECK_NEAR(s.x0, 10.0, 1e-5);
    CHECK_NEAR(s.x1, 30.0, 1e-5);
    s = L.spanFor(40, 50);                           // clipped on the left
    CHECK(s.visible);
    CHECK_NEAR(s.x0, 0.0, 1e-5);
    CHECK(!L.spanFor(10, 20).visible);               // entirely off-screen
}

static void testStripHitTesting()
{
    std::cout << "testStripHitTesting\n";
    const auto L = testLayout();
    InstrumentMap m;
    m.zones.push_back(makeZone("a", 48, 52, 50));          // C3-E3, root D3
    m.zones.push_back(makeZone("b", 53, 59, 55));          // F3-B3, root G3
    m.zones.push_back(makeZone("c", 60, 60, 60));          // single key C4
    m.zones.push_back(makeZone("d", 48, 52, 50, 1, 64));   // velocity layer stacked on "a"
    const auto spans = spansOf(m);
    ZoneSelection none;

    // Left edge of a
    auto h = hitTestStrip(L, spans, none, 1.0f, 30.0f);
    CHECK_EQ(h.zone, 0);
    CHECK(h.part == StripPart::LowEdge);

    // a's high edge and b's low edge share x = 30: the selection decides, else map order
    h = hitTestStrip(L, spans, none, 30.0f, 40.0f);
    CHECK_EQ(h.zone, 0);
    CHECK(h.part == StripPart::HighEdge);
    h = hitTestStrip(L, spans, sel({ 1 }, 1), 30.0f, 40.0f);
    CHECK_EQ(h.zone, 1);
    CHECK(h.part == StripPart::LowEdge);
    h = hitTestStrip(L, spans, sel({ 0 }, 0), 31.0f, 40.0f);
    CHECK_EQ(h.zone, 0);
    CHECK(h.part == StripPart::HighEdge);

    // Root marker (dot near the bottom of D3)
    h = hitTestStrip(L, spans, none, L.keyCentre(50), L.rootMarkerY(50));
    CHECK_EQ(h.zone, 0);
    CHECK(h.part == StripPart::Root);
    // ...the same key higher up is the body
    h = hitTestStrip(L, spans, none, L.keyCentre(50), 35.0f);
    CHECK(h.part == StripPart::Body);
    CHECK_EQ(h.key, 50);

    // Body of a stacked pair: map order unless the other one is selected
    h = hitTestStrip(L, spans, none, 22.0f, 45.0f);
    CHECK_EQ(h.zone, 0);
    CHECK(h.part == StripPart::Body);
    h = hitTestStrip(L, spans, sel({ 3 }, 3), 22.0f, 45.0f);
    CHECK_EQ(h.zone, 3);

    // Narrow single-key zone: edge grab shrinks so the middle is still the body
    const float cx = (L.spanFor(60, 60).x0 + L.spanFor(60, 60).x1) * 0.5f;
    h = hitTestStrip(L, spans, sel({ 2 }, 2), cx, 30.0f);
    CHECK_EQ(h.zone, 2);
    CHECK(h.part == StripPart::Body);
    h = hitTestStrip(L, spans, sel({ 2 }, 2), L.spanFor(60, 60).x0 + 1.0f, 30.0f);
    CHECK(h.part == StripPart::LowEdge);

    // Empty area
    h = hitTestStrip(L, spans, none, 140.0f, 45.0f);
    CHECK_EQ(h.zone, -1);
    CHECK(h.part == StripPart::None);

    // A zone clipped by the visible range has no grabbable edge on the clipped side
    InstrumentMap wide;
    wide.zones.push_back(makeZone("w", 30, 55, 50));
    h = hitTestStrip(L, spansOf(wide), none, 1.0f, 40.0f);
    CHECK(h.part == StripPart::Body);
}

static void testStripDragMath()
{
    std::cout << "testStripDragMath\n";
    InstrumentMap m;
    m.zones.push_back(makeZone("a", 48, 52, 50));
    m.zones.push_back(makeZone("b", 53, 59, 55));
    const auto one = sel({ 0 }, 0);

    // Low edge follows the pointer key
    auto c = computeStripDrag(m, one, StripPart::LowEdge, 48, 45);
    CHECK_EQ(c.size(), size_t(1));
    CHECK_EQ(changed(c, 0)->keyLow, 45);
    CHECK_EQ(changed(c, 0)->keyHigh, 52);
    // Past the high edge: no inversion, the high edge is pushed along (editor rule)
    c = computeStripDrag(m, one, StripPart::LowEdge, 48, 56);
    CHECK_EQ(changed(c, 0)->keyLow, 56);
    CHECK_EQ(changed(c, 0)->keyHigh, 56);
    // Dragging back is computed from the mouse-down zones: the high edge comes back
    c = computeStripDrag(m, one, StripPart::LowEdge, 48, 47);
    CHECK_EQ(changed(c, 0)->keyLow, 47);
    CHECK_EQ(changed(c, 0)->keyHigh, 52);
    // High edge clamped to 127 / low edge to 0
    c = computeStripDrag(m, one, StripPart::HighEdge, 52, 300);
    CHECK_EQ(changed(c, 0)->keyHigh, 127);
    c = computeStripDrag(m, one, StripPart::LowEdge, 48, -20);
    CHECK_EQ(changed(c, 0)->keyLow, 0);
    // High edge below the low edge drags the low edge down
    c = computeStripDrag(m, one, StripPart::HighEdge, 52, 40);
    CHECK_EQ(changed(c, 0)->keyLow, 40);
    CHECK_EQ(changed(c, 0)->keyHigh, 40);

    // Root marker
    c = computeStripDrag(m, one, StripPart::Root, 50, 61);
    CHECK_EQ(changed(c, 0)->rootKey, 61);
    CHECK_EQ(changed(c, 0)->keyLow, 48);
    CHECK_EQ(changed(c, 0)->keyHigh, 52);

    // Body: range and root move together, width kept
    c = computeStripDrag(m, one, StripPart::Body, 50, 53);
    CHECK_EQ(changed(c, 0)->keyLow, 51);
    CHECK_EQ(changed(c, 0)->keyHigh, 55);
    CHECK_EQ(changed(c, 0)->rootKey, 53);
    c = computeStripDrag(m, one, StripPart::Body, 50, -100);   // clamped at key 0, width kept
    CHECK_EQ(changed(c, 0)->keyLow, 0);
    CHECK_EQ(changed(c, 0)->keyHigh, 4);
    CHECK_EQ(changed(c, 0)->rootKey, 2);

    // Multi-select body move: one clamp for the whole group
    const auto both = sel({ 0, 1 }, 0);
    c = computeStripDrag(m, both, StripPart::Body, 50, 127);
    CHECK_EQ(c.size(), size_t(2));
    CHECK_EQ(changed(c, 1)->keyHigh, 127);           // b hits the top...
    CHECK_EQ(changed(c, 1)->keyLow, 121);
    CHECK_EQ(changed(c, 0)->keyLow, 116);            // ...a moved by the same 68 keys
    CHECK_EQ(changed(c, 0)->keyHigh, 120);

    // Multi-select edge: the dragged (primary) zone's edge follows, the others move by as much
    c = computeStripDrag(m, sel({ 0, 1 }, 1), StripPart::LowEdge, 53, 51);
    CHECK_EQ(changed(c, 1)->keyLow, 51);
    CHECK_EQ(changed(c, 0)->keyLow, 46);
    CHECK_EQ(changed(c, 0)->keyHigh, 52);

    // Alt-draw: every selected zone gets the drawn range (drawn right-to-left here)
    c = computeStripDrag(m, both, StripPart::DrawRange, 70, 64);
    CHECK_EQ(changed(c, 0)->keyLow, 64);
    CHECK_EQ(changed(c, 0)->keyHigh, 70);
    CHECK_EQ(changed(c, 1)->keyLow, 64);
    CHECK_EQ(changed(c, 0)->rootKey, 50);            // root untouched
    c = computeStripDrag(m, one, StripPart::DrawRange, 130, 140);
    CHECK_EQ(changed(c, 0)->keyLow, 127);            // clamped

    // Nothing selected -> nothing to do
    CHECK(computeStripDrag(m, ZoneSelection {}, StripPart::Body, 50, 52).empty());

    // Names and tooltips (C4 = 60 vs C3 = 60)
    CHECK_EQ(std::string(stripDragActionName(StripPart::Body, false)), std::string("Move zone"));
    CHECK_EQ(std::string(stripDragActionName(StripPart::LowEdge, true)), std::string("Drag low keys"));
    const Zone z = makeZone("a", 48, 52, 50);
    const auto lab4 = stripDragLabel(StripPart::LowEdge, z, true);
    const auto lab3 = stripDragLabel(StripPart::LowEdge, z, false);
    CHECK(lab4.rfind("Low C3", 0) == 0);
    CHECK(lab4.find("C3\xe2\x80\x93" "E3") != std::string::npos);
    CHECK(lab3.rfind("Low C2", 0) == 0);
    CHECK(lab3.find("C2\xe2\x80\x93" "E2") != std::string::npos);
    CHECK(stripDragLabel(StripPart::Root, z, true).find("Root D3 (50)") != std::string::npos);
    CHECK(stripDragLabel(StripPart::Body, z, true, 3).find("3 zones") != std::string::npos);
}

static void testSelectionModel()
{
    std::cout << "testSelectionModel\n";
    ZoneSelection s;
    CHECK(s.empty());
    CHECK_EQ(s.primary(), -1);
    s.selectOnly(4);
    CHECK_EQ(s.size(), size_t(1));
    CHECK_EQ(s.primary(), 4);
    s.toggle(2);                          // Cmd/Ctrl-click adds and becomes primary
    CHECK_EQ(s.size(), size_t(2));
    CHECK_EQ(s.primary(), 2);
    CHECK(s.isMulti());
    CHECK(s.indices() == (std::vector<size_t> { 2, 4 }));
    s.toggle(7);
    s.toggle(7);                          // removing the primary promotes a neighbour
    CHECK(!s.contains(7));
    CHECK(s.primary() == 4 || s.primary() == 2);
    s.toggle(2);
    s.toggle(4);
    CHECK(s.empty());
    CHECK_EQ(s.primary(), -1);

    // Shift-click range in display (keyboard) order, anchored at the last plain click
    const std::vector<size_t> order { 5, 0, 3, 1, 4, 2 };
    s.selectOnly(0);
    s.selectRange(order, 4);
    CHECK(s.indices() == (std::vector<size_t> { 0, 1, 3, 4 }));
    CHECK_EQ(s.primary(), 4);
    s.selectRange(order, 5);              // re-spans from the same anchor (0)
    CHECK(s.indices() == (std::vector<size_t> { 0, 5 }));
    ZoneSelection fresh;
    fresh.selectRange(order, 3);          // no anchor -> plain select
    CHECK(fresh.indices() == (std::vector<size_t> { 3 }));

    // applySelectionClick dispatch
    ZoneSelection c;
    applySelectionClick(c, 1, ClickModifier::None, order);
    applySelectionClick(c, 2, ClickModifier::Toggle, order);
    CHECK(c.indices() == (std::vector<size_t> { 1, 2 }));
    applySelectionClick(c, 3, ClickModifier::None, order);
    CHECK(c.indices() == (std::vector<size_t> { 3 }));
    applySelectionClick(c, 2, ClickModifier::Range, order);   // order: 3, 1, 4, 2
    CHECK(c.indices() == (std::vector<size_t> { 1, 2, 3, 4 }));

    // Select All, set, setPrimary, prune
    c.selectAll(6);
    CHECK_EQ(c.size(), size_t(6));
    CHECK_EQ(c.primary(), 2);             // primary kept
    c.setPrimary(5);
    CHECK_EQ(c.primary(), 5);
    c.setPrimary(9);                      // not selected: ignored
    CHECK_EQ(c.primary(), 5);
    c.prune(4);
    CHECK(c.indices() == (std::vector<size_t> { 0, 1, 2, 3 }));
    CHECK(c.primary() >= 0 && c.primary() < 4);
    c.set({ 3, 1, 3 }, 7);                // duplicates removed, invalid primary -> first
    CHECK(c.indices() == (std::vector<size_t> { 1, 3 }));
    CHECK_EQ(c.primary(), 1);
    c.selectAll(0);
    CHECK(c.empty());
}

static void testMultiSelectEdits()
{
    std::cout << "testMultiSelectEdits\n";
    InstrumentMap m;
    m.zones.push_back(makeZone("a", 48, 52, 50));
    m.zones.push_back(makeZone("b", 53, 59, 55));
    m.zones.push_back(makeZone("c", 60, 64, 62));
    m.zones[0].tuneCents = -10.0f;
    m.zones[1].tuneCents = 0.0f;
    m.zones[2].tuneCents = 97.0f;
    m.zones[0].gainDb = -3.0f;
    m.zones[1].gainDb = -3.0f;
    m.zones[2].gainDb = -3.0f;
    const std::vector<size_t> all { 0, 1, 2 };

    // Mixed vs uniform
    auto s = summarizeField(m, all, ZoneField::TuneCents, 1);
    CHECK(s.mixed);
    CHECK_EQ(s.count, size_t(3));
    CHECK_NEAR(s.first, 0.0, 1e-9);                   // primary's value
    CHECK_NEAR(s.minValue, -10.0, 1e-6);
    CHECK_NEAR(s.maxValue, 97.0, 1e-6);
    s = summarizeField(m, all, ZoneField::GainDb);
    CHECK(!s.mixed);
    CHECK_NEAR(s.first, -3.0, 1e-6);
    CHECK(!summarizeField(m, { 1 }, ZoneField::RootKey).mixed);
    CHECK(summarizeField(m, all, ZoneField::RootKey).mixed);

    // Relative: fine tune +5 on every zone, each from its own value, clamped at +100
    auto c = offsetField(m, all, ZoneField::TuneCents, 5.0);
    CHECK_EQ(c.size(), size_t(3));
    CHECK_NEAR(changed(c, 0)->tuneCents, -5.0, 1e-6);
    CHECK_NEAR(changed(c, 1)->tuneCents, 5.0, 1e-6);
    CHECK_NEAR(changed(c, 2)->tuneCents, 100.0, 1e-6);
    // Gain +2 dB
    c = offsetField(m, all, ZoneField::GainDb, 2.0);
    for (const auto& x : c)
        CHECK_NEAR(x.zone.gainDb, -1.0, 1e-6);
    // Roots shift by N, never inverting anything
    c = offsetField(m, { 0, 2 }, ZoneField::RootKey, 12.0);
    CHECK_EQ(changed(c, 0)->rootKey, 62);
    CHECK_EQ(changed(c, 2)->rootKey, 74);
    CHECK(changed(c, 1) == nullptr);                 // unselected zone untouched

    // Absolute: velocity layer for every zone
    c = setFieldAll(m, all, ZoneField::VelLow, 64);
    for (const auto& x : c)
    {
        CHECK_EQ(x.zone.velLow, 64);
        CHECK_EQ(x.zone.velHigh, 127);
    }
    c = setFieldAll(m, all, ZoneField::VelHigh, 0);   // clamped to 1, low pushed down
    for (const auto& x : c)
    {
        CHECK_EQ(x.zone.velHigh, 1);
        CHECK_EQ(x.zone.velLow, 1);
    }
    CHECK(fieldEditsRelative(ZoneField::GainDb));
    CHECK(fieldEditsRelative(ZoneField::KeyLow));
    CHECK(!fieldEditsRelative(ZoneField::VelLow));
    CHECK(!fieldEditsRelative(ZoneField::RrGroup));

    // Shift keys by N: joint clamp keeps the layout
    CHECK_EQ(clampKeyShift(m, all, 12), 12);
    CHECK_EQ(clampKeyShift(m, all, 100), 127 - 64);
    CHECK_EQ(clampKeyShift(m, all, -100), -48);
    CHECK_EQ(clampKeyShift(m, {}, 5), 0);
    c = shiftKeys(m, all, -100);
    CHECK_EQ(changed(c, 0)->keyLow, 0);
    CHECK_EQ(changed(c, 2)->keyLow, 12);
    CHECK_EQ(changed(c, 2)->keyHigh, 16);
    CHECK_EQ(changed(c, 1)->rootKey, 7);

    // Typed text in a multi-selection
    auto t = classifyMultiEditText(" +5 ");
    CHECK(t.relative);
    CHECK_EQ(t.body, std::string("+5"));
    t = classifyMultiEditText("-3 dB");
    CHECK(t.relative);
    t = classifyMultiEditText("=-6");
    CHECK(!t.relative);
    CHECK_EQ(t.body, std::string("-6"));
    t = classifyMultiEditText("E3");
    CHECK(!t.relative);
    CHECK_EQ(t.body, std::string("E3"));
    t = classifyMultiEditText("12");
    CHECK(!t.relative);
}

static void testGroupEditRecords()
{
    std::cout << "testGroupEditRecords\n";
    InstrumentMap m;
    m.zones.push_back(makeZone("a", 48, 52, 50));
    m.zones.push_back(makeZone("b", 53, 59, 55));
    m.zones.push_back(makeZone("c", 60, 64, 62));

    std::vector<ZoneChange> ch { { 0, withZoneField(m.zones[0], ZoneField::GainDb, -6.0) },
                                 { 1, m.zones[1] },                              // unchanged: skipped
                                 { 2, withZoneField(m.zones[2], ZoneField::RootKey, 61) } };
    auto edits = makeZoneEdits(m, ch);
    CHECK_EQ(edits.size(), size_t(2));
    InstrumentMap work = m;
    CHECK(applyZoneEdits(work, edits, true));
    CHECK_NEAR(work.zones[0].gainDb, -6.0, 1e-6);
    CHECK_EQ(work.zones[2].rootKey, 61);
    CHECK(applyZoneEdits(work, edits, false));
    CHECK(sameZoneSettings(work.zones[0], m.zones[0]));
    CHECK(sameZoneSettings(work.zones[2], m.zones[2]));

    // A stale member rejects the whole group (never half-applies)
    std::vector<ZoneChange> stale = ch;
    stale[1].zone.sampleId = "zzz";
    CHECK(makeZoneEdits(m, stale).empty());
    InstrumentMap other = m;
    other.zones[2].sampleId = "replaced";
    CHECK(!applyZoneEdits(other, edits, true));
    CHECK_NEAR(other.zones[0].gainDb, 0.0, 1e-6);    // nothing applied
    // Out of range
    CHECK(makeZoneEdits(m, { { 9, m.zones[0] } }).empty());
    // A zone proposed twice: the later proposal wins
    edits = makeZoneEdits(m, { { 0, withZoneField(m.zones[0], ZoneField::GainDb, -6.0) },
                               { 0, withZoneField(m.zones[0], ZoneField::GainDb, -9.0) } });
    CHECK_EQ(edits.size(), size_t(1));
    CHECK_NEAR(edits[0].after.gainDb, -9.0, 1e-6);
    // Nothing changes -> no edits
    CHECK(makeZoneEdits(m, { { 0, m.zones[0] } }).empty());
}

static void testSavedStateTracker()
{
    std::cout << "testSavedStateTracker\n";
    EditStateTracker t;
    CHECK(!t.isDirty());
    const auto s0 = t.current();
    const auto s1 = t.allocate();
    t.moveTo(s1);                 // edit
    CHECK(t.isDirty());
    t.moveTo(s0);                 // undo back to the loaded state
    CHECK(!t.isDirty());
    t.moveTo(s1);
    t.markSaved();                // save after the edit
    CHECK(!t.isDirty());
    CHECK(t.atSavedState());
    t.moveTo(s0);                 // undo past the save point -> unsaved
    CHECK(t.isDirty());
    t.moveTo(s1);                 // redo -> clean again
    CHECK(!t.isDirty());
    const auto s2 = t.allocate();
    t.moveTo(s2);
    CHECK(t.isDirty());

    // Non-undoable change: dirty even when the zones are back at the saved state
    t.moveTo(s1);
    t.markExternalChange();
    CHECK(t.isDirty());
    CHECK(t.atSavedState());
    t.markSaved();
    CHECK(!t.isDirty());

    // Fresh import (dirty) / patch load (clean)
    t.reset(true);
    CHECK(t.isDirty());
    t.reset(false);
    CHECK(!t.isDirty());
    CHECK(t.allocate() != t.current());
}

static void testGainRampUnit()
{
    std::cout << "testGainRampUnit\n";
    GainRamp r;
    r.reset(1.0f);
    CHECK(!r.isRamping());
    CHECK_NEAR(r.next(), 1.0, 1e-9);
    const int n = GainRamp::samplesFor(48000.0);
    CHECK_EQ(n, 960);                                  // 20 ms
    CHECK_EQ(GainRamp::samplesFor(44100.0), 882);
    r.setTarget(4.0f, n);
    CHECK(r.isRamping());
    float prev = r.current;
    float maxStep = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v = r.next();
        maxStep = std::max(maxStep, std::fabs(v - prev));
        CHECK(v >= prev - 1e-6f);                      // monotonic
        prev = v;
    }
    CHECK_NEAR(prev, 4.0, 1e-6);                       // lands exactly on the target
    CHECK(!r.isRamping());
    CHECK_NEAR(maxStep, 3.0 / n, 1e-4);                // evenly spread, no jump
    r.setTarget(0.5f, 0);                              // zero length = jump
    CHECK_NEAR(r.next(), 0.5, 1e-9);
    // Retarget mid-ramp continues from where it is (no jump back)
    r.reset(1.0f);
    r.setTarget(2.0f, 100);
    for (int i = 0; i < 50; ++i)
        r.next();
    const float mid = r.current;
    r.setTarget(0.0f, 100);
    CHECK_NEAR(r.next(), mid - mid / 100.0f, 1e-5);
}

static SampleBuffer dcBuffer(float level, double seconds = 3.0, double sr = 44100.0)
{
    SampleBuffer b;
    b.channels = 1;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(seconds * sr);
    b.interleaved.assign(static_cast<size_t>(b.length), level);
    return b;
}

static void testGainRampOnSoundingNote()
{
    std::cout << "testGainRampOnSoundingNote\n";
    SamplePool pool;
    pool.setBuffer("dc", dcBuffer(0.25f));
    auto map = std::make_shared<InstrumentMap>();
    map->zones.push_back(makeZone("dc", 0, 127, 60));
    VoiceEngine e;
    e.setSampleRate(44100.0);
    e.setSamplePool(&pool);
    AmpEnv::Params env;
    env.attackMs = 1.0f;
    env.decayMs = 1.0f;
    env.sustain = 1.0f;
    env.releaseMs = 200.0f;
    e.setEnvParams(env);
    FilterParams fp;
    fp.cutoffHz = 20000.0f;
    fp.resonance = 0.0f;
    e.setFilterParams(fp);
    e.adoptMap(map);
    e.noteOn(60, 127, 1);
    std::vector<float> l(4096), r(4096);
    for (int i = 0; i < 4; ++i)
        e.processBlock(l.data(), r.data(), 1024);       // settle: attack, decay, filter
    const float before = l[1023];
    CHECK(before > 0.05f);

    // +12 dB on the sounding zone (live edit, as the zone editor does it)
    auto next = std::make_shared<InstrumentMap>(*map);
    next->zones[0].gainDb = 12.0f;
    e.updateMapLive(next);
    const long allocs0 = g_allocs.load();
    e.processBlock(l.data(), r.data(), 2048);
    CHECK_EQ(g_allocs.load() - allocs0, 0L);            // ramp is allocation-free

    const float target = before * std::pow(10.0f, 12.0f / 20.0f);
    const float jump = target - before;
    float maxStep = std::fabs(l[0] - before);
    for (int i = 1; i < 2048; ++i)
        maxStep = std::max(maxStep, std::fabs(l[static_cast<size_t>(i)] - l[static_cast<size_t>(i - 1)]));
    const int n = GainRamp::samplesFor(44100.0);
    CHECK(maxStep <= 1.5f * jump / static_cast<float>(n));  // no step discontinuity...
    CHECK(maxStep < jump * 0.01f);                      // ...where a hard switch would jump 100%
    CHECK_NEAR(l[static_cast<size_t>(n / 2)], before + jump * 0.5f, jump * 0.02f);  // linear, half way at 10 ms
    CHECK_NEAR(l[2047], target, target * 1e-3);          // settled on the new gain after 20 ms
    const Voice* v = e.findActiveVoice(60, 1);
    CHECK(v != nullptr);
    if (v)
    {
        CHECK(!v->zoneGain.isRamping());
        CHECK_NEAR(v->zoneGainLin, std::pow(10.0, 12.0 / 20.0), 1e-4);
    }

    // A fresh note starts directly at the zone gain (no fade-in from the ramp)
    e.noteOn(64, 127, 1);
    const Voice* v64 = e.findActiveVoice(64, 1);
    CHECK(v64 != nullptr);
    if (v64)
    {
        CHECK(!v64->zoneGain.isRamping());
        CHECK_NEAR(v64->zoneGain.current, std::pow(10.0, 12.0 / 20.0), 1e-4);
    }
}

static SampleBuffer toneBuffer(double seconds = 2.0, double sr = 44100.0)
{
    SampleBuffer b;
    b.channels = 1;
    b.sampleRate = sr;
    b.length = static_cast<int64_t>(seconds * sr);
    b.interleaved.resize(static_cast<size_t>(b.length));
    for (int64_t i = 0; i < b.length; ++i)
        b.interleaved[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
    return b;
}

static int auditionVoices(const VoiceEngine& e, int note, int* zoneOut = nullptr, std::string* sampleOut = nullptr)
{
    const Voice* v = e.findActiveVoice(note, VoiceEngine::kAuditionChannel);
    if (!v || v->releasing)
        return 0;
    if (zoneOut) *zoneOut = v->zoneIndex;
    if (sampleOut) *sampleOut = v->zone.sampleId;
    return 1;
}

static void testAuditionTargetsExactZone()
{
    std::cout << "testAuditionTargetsExactZone\n";
    SamplePool pool;
    for (const char* id : { "a", "b", "r0", "r1", "r2" })
        pool.setBuffer(id, toneBuffer());
    auto map = std::make_shared<InstrumentMap>();
    map->zones.push_back(makeZone("a", 48, 72, 60));                 // 0: first match at 60
    map->zones.push_back(makeZone("b", 48, 72, 60));                 // 1: overlaps a completely
    map->zones.push_back(makeZone("r0", 80, 80, 80, 1, 127, 1, 0));  // 2..4: RR group at 80
    map->zones.push_back(makeZone("r1", 80, 80, 80, 1, 127, 1, 1));
    map->zones.push_back(makeZone("r2", 80, 80, 80, 1, 127, 1, 2));
    map->zones.push_back(makeZone("offline", 90, 90, 90));           // 5: no audio loaded
    VoiceEngine e;
    e.setSampleRate(44100.0);
    e.setSamplePool(&pool);
    e.adoptMap(map);
    std::vector<float> l(256), r(256);
    auto block = [&] { e.processBlock(l.data(), r.data(), 256); };

    // Normal note 60 plays the first match ("a")...
    e.noteOn(60, 100, 1);
    CHECK_EQ(e.findActiveVoice(60, 1)->zone.sampleId, std::string("a"));
    e.noteOff(60, 1);

    // ...but auditioning zone 1 plays "b" exactly
    const long allocs0 = g_allocs.load();
    e.requestAudition(1, 60, 100);
    block();
    CHECK_EQ(g_allocs.load() - allocs0, 0L);             // lock-free, allocation-free hand-off
    int zi = -1;
    std::string sid;
    CHECK_EQ(auditionVoices(e, 60, &zi, &sid), 1);
    CHECK_EQ(zi, 1);
    CHECK_EQ(sid, std::string("b"));

    // Release
    e.stopAudition();
    block();
    CHECK_EQ(auditionVoices(e, 60), 0);

    // RR alternate r1 every time, and the group's cycle position is not advanced
    for (int k = 0; k < 3; ++k)
    {
        e.requestAudition(3, 80, 100);
        block();
        CHECK_EQ(auditionVoices(e, 80, &zi, &sid), 1);
        CHECK_EQ(sid, std::string("r1"));
        e.stopAudition();
        block();
    }
    CHECK_EQ(e.selectZone(80, 100)->sampleId, std::string("r0"));   // cycle still at its start

    // A newer request replaces an older one before the audio thread saw it
    e.requestAudition(0, 60, 100);
    e.requestAudition(4, 80, 100);
    block();
    CHECK_EQ(auditionVoices(e, 60), 0);
    CHECK_EQ(auditionVoices(e, 80, &zi, &sid), 1);
    CHECK_EQ(sid, std::string("r2"));
    // Switching zones releases the previous audition
    e.requestAudition(1, 60, 100);
    block();
    CHECK_EQ(auditionVoices(e, 80), 0);
    CHECK_EQ(auditionVoices(e, 60, &zi), 1);
    CHECK_EQ(zi, 1);
    e.stopAudition();
    block();

    // Audition voices never collide with host notes on MIDI channels
    e.noteOn(60, 100, 1);
    e.requestAudition(1, 60, 100);
    block();
    CHECK(e.findActiveVoice(60, 1) != nullptr);
    CHECK_EQ(auditionVoices(e, 60), 1);
    e.noteOff(60, 1);                                   // host note-off leaves the audition alone
    CHECK_EQ(auditionVoices(e, 60), 1);
    e.stopAudition();
    block();

    // Invalid / offline zones stay silent
    CHECK(!e.auditionZoneNow(99, 60, 100));
    CHECK(!e.auditionZoneNow(5, 90, 100));
    e.requestAudition(-1, 60, 100);                     // = stop
    block();
    CHECK_EQ(auditionVoices(e, 60), 0);

    // Velocity / note come from auditionNoteFor (root clamped into range, layer velocity)
    int note = 0, vel = 0;
    auditionNoteFor(makeZone("x", 48, 60, 72, 110, 127), note, vel);
    CHECK_EQ(note, 60);
    CHECK_EQ(vel, 110);
}

#if LOOPER_ZONE_EDIT_WITH_JUCE
/** Stands in for LooperAudioProcessor (map + saved-state tracker, live map pushes). */
struct FakeTarget : ZoneEditTarget
{
    SamplePool pool;
    VoiceEngine engine;
    std::shared_ptr<InstrumentMap> map = std::make_shared<InstrumentMap>();
    EditStateTracker state;
    int publishes = 0;

    FakeTarget()
    {
        map->zones.push_back(makeZone("a", 48, 52, 50));
        map->zones.push_back(makeZone("b", 53, 59, 55));
        map->zones.push_back(makeZone("c", 60, 64, 62));
        engine.setSamplePool(&pool);
        engine.adoptMap(map);
    }
    bool replaceZone(size_t index, const std::string& sampleId, const Zone& zone) override
    {
        auto next = std::make_shared<InstrumentMap>(*map);
        if (!replaceZoneInMap(*next, index, sampleId, zone))
            return false;
        map = next;
        engine.updateMapLive(next);
        ++publishes;
        return true;
    }
    bool replaceZones(const std::vector<ZoneEdit>& edits, bool forward) override
    {
        auto next = std::make_shared<InstrumentMap>(*map);
        if (!applyZoneEdits(*next, edits, forward))
            return false;
        map = next;
        engine.updateMapLive(next);
        ++publishes;
        return true;
    }
    bool perform(juce::UndoManager& um, const std::vector<ZoneChange>& changes, bool newTransaction = true)
    {
        auto edits = makeZoneEdits(*map, changes);
        if (edits.empty())
            return false;
        if (newTransaction)
            um.beginNewTransaction();
        return um.perform(new ZoneEditAction(*this, std::move(edits), &state));
    }
    const Zone& zone(size_t i) const { return map->zones[i]; }
};

static void testUndoGroupingAndSavedState()
{
    std::cout << "testUndoGroupingAndSavedState\n";
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeTarget t;
    juce::UndoManager um;
    const std::vector<size_t> all { 0, 1, 2 };

    // Multi-select relative edit = one action, one live publish, one undo step
    CHECK(t.perform(um, offsetField(*t.map, all, ZoneField::GainDb, 2.0)));
    CHECK_EQ(um.getNumActionsInCurrentTransaction(), 1);
    CHECK_EQ(t.publishes, 1);
    for (size_t i : all)
        CHECK_NEAR(t.zone(i).gainDb, 2.0, 1e-6);
    CHECK(t.state.isDirty());
    CHECK(um.undo());
    for (size_t i : all)
        CHECK_NEAR(t.zone(i).gainDb, 0.0, 1e-6);
    CHECK(!t.state.isDirty());                           // back at the loaded state
    CHECK(!um.canUndo());
    CHECK(um.redo());
    CHECK_NEAR(t.zone(2).gainDb, 2.0, 1e-6);

    // Strip drag on a 2-zone selection: many moves, one transaction, coalesced to one action
    const InstrumentMap origin = *t.map;
    const auto two = sel({ 0, 1 }, 0);
    bool open = false;
    for (int key = 51; key <= 58; ++key)
        if (t.perform(um, computeStripDrag(origin, two, StripPart::Body, 50, key), !open))
            open = true;
    CHECK_EQ(um.getNumActionsInCurrentTransaction(), 1);
    CHECK_EQ(t.zone(0).keyLow, 56);
    CHECK_EQ(t.zone(1).keyHigh, 67);
    CHECK(um.undo());                                    // whole drag undone in one step
    CHECK_EQ(t.zone(0).keyLow, 48);
    CHECK_EQ(t.zone(1).keyHigh, 59);
    CHECK_NEAR(t.zone(0).gainDb, 2.0, 1e-6);             // previous step untouched

    // A drag whose group changes mid-way (a zone clamps and drops out) is still one step
    open = false;
    const InstrumentMap o2 = *t.map;
    CHECK(t.perform(um, computeStripDrag(o2, two, StripPart::LowEdge, 48, 45), true));
    CHECK(t.perform(um, setFieldAll(*t.map, { 0 }, ZoneField::RootKey, 47), false));
    CHECK_EQ(um.getNumActionsInCurrentTransaction(), 2);
    CHECK(um.undo());
    CHECK_EQ(t.zone(0).keyLow, 48);
    CHECK_EQ(t.zone(0).rootKey, 50);
    CHECK_EQ(t.zone(1).keyLow, 53);

    // Saved state: save, edit, undo -> clean; undo past save -> dirty; redo -> clean
    t.state.markSaved();
    CHECK(!t.state.isDirty());
    CHECK(t.perform(um, setFieldAll(*t.map, { 1 }, ZoneField::TuneCents, 7.0)));
    CHECK(t.state.isDirty());
    CHECK(um.undo());
    CHECK(!t.state.isDirty());                           // the "unsaved" marker clears
    CHECK(um.undo());                                    // past the save point (gain +2 undone)
    CHECK(t.state.isDirty());
    CHECK(um.redo());
    CHECK(!t.state.isDirty());
    // Branching after an undo: a different edit is not the saved state
    CHECK(t.perform(um, setFieldAll(*t.map, { 1 }, ZoneField::TuneCents, -7.0)));
    CHECK(t.state.isDirty());
    CHECK(um.undo());
    CHECK(!t.state.isDirty());
    // A coalesced drag undone as one step returns to the saved state too
    open = false;
    const InstrumentMap o3 = *t.map;
    for (int key = 46; key >= 40; --key)
        if (t.perform(um, computeStripDrag(o3, sel({ 2 }, 2), StripPart::HighEdge, 64, key), !open))
            open = true;
    CHECK(t.state.isDirty());
    CHECK(um.undo());
    CHECK(!t.state.isDirty());
    CHECK_EQ(t.zone(2).keyHigh, 64);
}
#endif

int main()
{
    testStripGeometryAndSnapping();
    testStripHitTesting();
    testStripDragMath();
    testSelectionModel();
    testMultiSelectEdits();
    testGroupEditRecords();
    testSavedStateTracker();
    testGainRampUnit();
    testGainRampOnSoundingNote();
    testAuditionTargetsExactZone();
#if LOOPER_ZONE_EDIT_WITH_JUCE
    testUndoGroupingAndSavedState();
#else
    std::cout << "(juce::UndoManager grouping tests skipped: built without JUCE)\n";
#endif
    std::cout << "\nZoneStripTests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
