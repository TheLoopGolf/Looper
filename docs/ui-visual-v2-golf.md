# Looper UI v2 — Modern golf-themed Look

**Status:** locked for implementation  
**Goal:** Feel like a 2025 commercial instrument plugin (Serum/Vital/Kontakt-adjacent polish), branded for Loop Audio Lab / The Loop Golf — premium course DNA, not cartoon golf balls.

## Palette (fairway night)

| Token | Hex | Use |
|-------|-----|-----|
| bg | `#0A1610` | Window background |
| bgRaised | `#12261C` | Cards / panels |
| bgSunken | `#08120D` | Drop zone, lists |
| border | `#1E3A2C` | Hairlines |
| borderBright | `#2F5A42` | Hover / focus rings |
| fairway | `#3DDC87` | Primary accent (arcs, active) |
| fairwayDim | `#1F8A52` | Secondary fills |
| brass | `#C9A227` | Wordmark underline, badges, warnings soft |
| sand | `#E8C547` | Caution / offline |
| text | `#E8EDE8` | Primary labels |
| muted | `#8A9A8E` | Secondary |
| danger | `#E85D4C` | Destructive / hard warn |

## Typography
- Wordmark: bold condensed tracking, “LOOPER” + small “Loop Audio Lab”
- Section titles: 11–12 px uppercase muted with letter-spacing
- Knob labels: short caps under dials
- Prefer system sans (JUCE default); no custom font files in v2

## Chrome (modern plugin cues)
1. **Custom LookAndFeel** — no stock JUCE rotary look. Thick track + fairway value arc, brass tip optional, value in text box below.
2. **Pill / rounded buttons** — filled fairway for primary (Accept), ghost border for secondary, brass outline for Open/Save.
3. **Header bar** — left wordmark + thin brass pin/flag glyph (vector path, ~16px), right actions + gear; subtle bottom gradient line fairway→transparent.
4. **Cards** — 10–12 px corner radius, soft inner shadow optional (simple darker inset stroke).
5. **Drop zone** — dashed fairway border, centered icon (flagstick path or upload chevron), larger type; drag = solid fairway glow.
6. **Zone keyboard strip** — real drawn keys (C2–C7 or full 0–127 compressed): white/black keys, zone spans as fairway/brass translucent overlays; empty state shows muted keys + “zones appear after import”.
7. **Performance deck** — grouped: AMP | FILTER with thin divider; filter mode as segmented control look (LP/HP/BP).
8. **Window size** — 1000 × 680 default.

## Tone / copy
- Tagline can stay; drop hint: “Drop a sample folder — AutoMapper builds the map.”
- Settings gear opens **in-plugin SettingsView** (Engine/Mapping…), not only JUCE Options.

## Non-goals v2
- No photo textures of grass, no emoji golf balls, no animated flag waving.
- No redesign of DSP/params — visual + layout only.
- No new product features beyond keyboard viz + LookAndFeel.

## Files to touch
- `Source/UI/LooperLookAndFeel.h/.cpp` (new)
- `Source/UI/ZoneKeyboardComponent.h/.cpp` (new)
- `MainView`, `ReviewMapView`, `SettingsView`, `PluginEditor`
- `CMakeLists.txt` — add new sources to plugin target
- Optional: refresh `docs/wireframes` later (screenshots after build)

## Success
Standalone builds; empty + settings screens look golf-branded and “premium dark”; knobs use custom LAF; zone strip draws keys; tests still pass.
