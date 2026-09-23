# Looper v1 — UI wireframe notes

## Principles
- One primary surface: play + light edit
- Mapping is a **review pass**, not the home screen
- Dark, calm studio tool — not a skeuomorphic keyboard toy
- Empty state teaches the product in one drop zone

## Screens
1. **Main empty** — large drop target, dormant keyboard strip, performance knobs present but unused
2. **Main loaded** — patch bar, Review map / + Samples / Save, zone keyboard, sample list, zone waveform detail, live knobs
3. **Review map** — table of auto-map results with source + confidence; Accept map / Back to play

## Interaction notes
- After first successful import, drop zone collapses into `+ Samples`
- Keyboard strip shows velocity-layer coloring for selected root
- Zone detail appears only when a zone/sample is selected
- Host-automatable knobs stay on the main view always

## Files
- `wireframes/01-main-empty.png`
- `wireframes/02-main-loaded.png`
- `wireframes/03-review-map.png`


## Screen 4 — Settings / preferences
- Left nav: Engine, Mapping, MIDI, Files, About
- Engine: polyphony, interpolation, glide, soft-clip, default filter
- Mapping: C4=60, key span, RR mode, velocity curve, unpitched fallback, open review after import
- MIDI / Files / About: same chrome; content deferred to implementation notes
- Global/session prefs — not a replacement for main-view performance knobs

Files:
- `wireframes/04-settings.png` (Engine)
- `wireframes/04b-settings-mapping.png` (Mapping)
