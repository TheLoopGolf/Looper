# Looper

**Loop Audio Lab** — multi-sample instrument builder (working title).

Drag WAV/AIFF folders, AutoMapper builds keyzones / velocity layers / round-robins, play with ADSR + one multimode SVF filter. **VST3 + AU**. JUCE 8 · C++20.

> v1 does **not** include granular, timestretch, scripting, or an FX chain.

Product & DSP design (authoritative): [`docs/PRODUCT.md`](docs/PRODUCT.md), [`docs/dsp-and-automapper-v1.md`](docs/dsp-and-automapper-v1.md).

---

## Defaults (locked)

| Topic | Value |
|-------|--------|
| Middle C | **C4 = MIDI 60** (scientific) |
| Key span | Full 0–127 |
| RR mode | Cycle |
| Interpolation | **4-point Hermite** |
| Glide | Off |
| Filter env source | Amp ADSR (octave-scaled by Filter Env Amt) |
| Formats | VST3 + AU (Standalone also enabled for debugging) |
| Manufacturer | Loop Audio Lab · codes `Loop` / `LoAp` |

---

## Module map

```
Source/
  AutoMapper/       Filename parse + zone building (implemented); YIN stub
  InstrumentMap/    Zone, SampleRef, InstrumentMap types
  SamplePool/       RAM SampleBuffer pool + demo tone generator
  VoiceEngine/      Polyphony, steal, Hermite + AmpEnv + SVF; vel layers + RR cycle
  AmpEnv/           Linear ADSR (min attack 0.1 ms)
  Filter/           Linear Simper SVF (LP/HP/BP, dual-state stereo)
  MidiRouter/       Note/CC + pitch bend (±2 st); CC1 → cutoff when target FilterCutoff
  PatchStore/       JSON sidecar (.looper.json) — relative sample paths, schema v1
  UI/               MainView (drop/loaded), ReviewMapView (table), SettingsView
  Prefs/            SessionPrefs (global/session settings JSON)
  Import/           ImportController + MapCommit (message-thread pipeline)
  Plugin/           PluginProcessor + PluginEditor (APVTS ADSR/Volume/Filter)
Tests/
  AutoMapperTests.cpp
  DspVoiceTests.cpp   Hermite / AmpEnv / SVF / offline VoiceEngine
  PatchStoreTests.cpp JSON round-trip + relative paths
  SessionPrefsTests.cpp prefs serialize round-trip
docs/               Product, DSP, wireframes
```

Libraries: `LooperAutoMapper` (filename map), `LooperDsp` (Hermite/AmpEnv/SamplePool/VoiceEngine/SVF, **no JUCE**), `LooperPatch` (JSON patch IO + SessionPrefs, **no JUCE**). Plugin links all three.

### Implemented vs TODO

| Area | Status |
|------|--------|
| Filename → note / vel / RR tokens | **Done** |
| Velocity layer midpoints, key midpoints, RR groups | **Done** |
| Duplicate warnings, deterministic maps | **Done** |
| YIN pitch detect | Stub (null / low confidence) |
| Hermite resampling + voice steal | **Done** |
| Demo RAM sample + single full-range zone | **Done** |
| Linear AmpEnv ADSR + APVTS params | **Done** |
| SVF (LP/HP/BP) + cutoff / res / env amt APVTS | **Done** |
| Map RR cycle / velocity layers at play | **Done** (cycle RR; vel layers by range) |
| Streaming sample I/O | TODO |
| Patch JSON save/load + host state | **Done** |
| WAV/AIFF/FLAC RAM load on import | **Done** |
| Drag-drop import → AutoMapper → Review → Accept | **Done** |
| MainView empty/loaded + ReviewMapView table | **Done** |
| Settings (Engine / Mapping / MIDI / Files / About) | **Done** |
| Keyboard strip graphic | TODO (polish) |
| Locate missing files (relocate UI) | TODO (offline list on load) |

---


## Hear sound (Standalone / VST3)

### Demo tone (no files)
1. Build with `LOOPER_BUILD_PLUGIN=ON`.
2. Run the **Standalone** target (or load the VST3).
3. On prepare, a short (~0.4 s) decaying C4 demo tone is loaded — play MIDI to hear it.

### Drag-drop → review → Accept (primary flow)
1. Drop a folder of WAV/AIFF/FLAC samples on the main UI (or click the drop zone / **+ Samples**).
2. Import runs on the **message thread** (decode → `SamplePool` → `AutoMapper`).
3. **Review map** opens with Sample / Source / Root / Vel / RR / Key span / Confidence.
4. Click **Accept map** to commit zones into the playable `InstrumentMap`, or **Back to play** to discard.
5. After Accept, the main view shows zone/root counts, sample list, **Review map** (re-open last result), and **+ Samples**.

Host-automatable params: **Attack / Decay / Sustain / Release / Volume / Filter Type / Cutoff / Resonance / Filter Env Amt**.

### Save / Load patch (Standalone or plugin UI)

1. Import + **Accept map** so you have a user instrument (Save is enabled).
2. Click **Save** → choose `Something.looper.json` (or `.json`). Sample paths are stored **relative to the patch file’s directory** when possible; audio is **not** embedded.
3. Click **Open…** (available even on the empty drop screen) → pick a `.looper.json` / `.json`. Samples are decoded on the message thread into `SamplePool`; missing files are listed as **offline** (zones kept; full relocate UI later).
4. Host project save/recall (`getStateInformation` / `setStateInformation`) embeds APVTS + a patch JSON blob. If sample paths from the previous session are still valid, buffers reload; otherwise zones stay with offline markers.

**Format sketch (schemaVersion 1):** `name`, `patchRoot`, `samples[]` (`id`, `path`, …), `map` (globals + `zones[]`), optional `params` (APVTS float snapshot).

**Limitations (v1):** no sample embedding; no relocate/browse-for-missing UI yet; streaming long files still TODO.


## Build

JUCE is pulled via **CMake FetchContent** (not vendored). Requires CMake ≥ 3.22 and a C++20 compiler.

### DSP + AutoMapper unit tests (any OS, no GUI deps)

```bash
cmake -S . -B build -DLOOPER_BUILD_PLUGIN=OFF -DLOOPER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
# or: ./build/AutoMapperTests && ./build/DspVoiceTests && ./build/PatchStoreTests && ./build/SessionPrefsTests
```

### Full plugin (macOS / Windows recommended)

```bash
cmake -S . -B build -DLOOPER_BUILD_PLUGIN=ON -DLOOPER_BUILD_TESTS=ON
cmake --build build --config Release
```

- **macOS**: builds AU + VST3 (+ Standalone). Xcode / Ninja + macOS SDK required for AU.
- **Windows**: VST3 (+ Standalone). Visual Studio 2022 recommended.
- **Linux**: VST3 + Standalone (AU is Apple-only). Needs typical JUCE GUI deps (`libasound2-dev`, `libfreetype-dev`, `libfontconfig1-dev`, X11/GL). WebKit is optional here (`JUCE_WEB_BROWSER=0`). If configure fails on a minimal box, use `LOOPER_BUILD_PLUGIN=OFF` for library/tests-only work.

First configure downloads JUCE **8.0.6** into the CMake build `_deps` tree (git-ignored).

---

## Settings / preferences

Open **⚙** (top-right on the main view) in Standalone or the plugin editor. **← Back to play** returns to MainView.

| Tab | Controls |
|-----|----------|
| **Engine** | Polyphony (1–128) → map + voice engine; Interpolation (Hermite; sinc later); Glide ms (stored on map; engine portamento TODO); Master soft-clip On/Off (tanh on voice sum); Default filter LP/HP/BP → APVTS |
| **Mapping** | Middle C C4=60 (locked); Key span full 0–127 vs natural; RR Cycle (Random later); Velocity curve Linear/Soft/Hard; Unpitched equal-spread + warn; Open review after import On/Off |
| **MIDI** | Pitch bend ±2 (stored; engine uses range); Mod target FilterCutoff / Volume; Sustain CC64 note (TODO); Clear MIDI learn stub |
| **Files** | Last patch path; missing-file offline policy; Reveal last patch folder |
| **About** | Looper / Loop Audio Lab / version / GitHub URL |

Session prefs persist in host state (`prefsJson` alongside patch). Mapping options feed the **next** AutoMapper import. ADSR / filter knobs stay on MainView.

## Status — drag-drop + review UI

**Done:** drop/browse import → AutoMapper → Review map → Accept into playable map; APVTS knobs; thread-safe map swap via `shared_ptr`/`adoptMap`; Settings screen with session prefs.

**Polish TODOs:** keyboard strip graphic, locate/relocate missing files UI, YIN pitch detect (stub today), glide/portamento DSP, sustain pedal, MIDI learn.

## Next milestone

Streaming sample I/O → relocate missing samples UI → optional dedicated filter envelope → YIN when filename pitch is missing.

## Contributing / next steps

1. Keyboard strip zone visualization per wireframes.
2. Replace pitch stub with YIN when filename pitch is missing.
3. Relocate UI for offline samples after patch/host load.
4. Parameter smoothing on continuous filter/env params; finish glide DSP.
5. Choose LICENSE compatible with your JUCE license (GPL vs commercial).
