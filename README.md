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
| RR mode | Cycle (per-patch switch to **Random**, see below) |
| Interpolation | **4-point Hermite** |
| Glide | Off |
| Filter env source | Amp ADSR (octave-scaled by Filter Env Amt) |
| Formats | VST3 + AU (Standalone also enabled for debugging) |
| Manufacturer | Loop Audio Lab · codes `Loop` / `LoAp` |

---

## Module map

```
Source/
  AutoMapper/       Filename parse + zone building + YIN pitch detection (PitchDetector)
  InstrumentMap/    Zone, SampleRef, InstrumentMap types
  SamplePool/       RAM SampleBuffer pool + demo tone generator
  VoiceEngine/      Polyphony, steal, Hermite + AmpEnv + SVF; vel layers + RR (Cycle / Random, FastRng)
  AmpEnv/           Linear ADSR (min attack 0.1 ms)
  Filter/           Linear Simper SVF (LP/HP/BP, dual-state stereo)
  MidiRouter/       Note/CC + pitch bend (±2 st); CC1 → cutoff when target FilterCutoff
  PatchStore/       JSON sidecar (.looper.json) — relative sample paths, schema v1;
                    SampleRelocator (find moved/missing samples, no JUCE)
  UI/               MainView (drop/loaded, ZONES list), ZoneEditorPanel, ZoneKeyboardComponent (clickable
                    strip), ReviewMapView (table), SettingsView, RelocateView;
                    LooperControls (segmented control, vector gear), Glyphs.h (all non-ASCII UI text)
  ZoneEdit/         ZoneEditor (clamp / no-inversion / overlaps / auto + detected actions / undoable
                    edit records, no JUCE) + ZoneEditAction (juce::UndoableAction wrapper)
  Prefs/            SessionPrefs (global/session settings JSON)
  Import/           ImportController + MapCommit (message-thread pipeline)
  Plugin/           PluginProcessor + PluginEditor (APVTS ADSR/Volume/Filter)
Tests/
  AutoMapperTests.cpp
  PitchDetectorTests.cpp YIN accuracy (sines/saws/plucks A0–C8), noise/silence/short/stereo
  DspVoiceTests.cpp   Hermite / AmpEnv / SVF / offline VoiceEngine
  PatchStoreTests.cpp JSON round-trip + relative paths
  SessionPrefsTests.cpp prefs serialize round-trip
  RelocatorTests.cpp  missing-sample search: case / tail match / ambiguity / cascade / Windows / unicode
  RoundRobinTests.cpp Cycle unchanged, Random no-repeat + uniformity, seeds, no-alloc, patch field
  PitchMappingTests.cpp drum-key spread (order / skips / shared layers), filename-vs-audio mismatch,
                    C3=60 naming, pitch metadata in .looper.json (round trip + old patches), prefs
  SourceEncodingTests.cpp mojibake guard: no raw non-ASCII literals; UI text only via Glyphs.h
  ZoneEditorTests.cpp field edits + clamping, no inverted ranges, overlap warnings, Reset to auto /
                    Use detected, .looper.json round trip, live voice updates (no audio-thread alloc),
                    audition note, juce::UndoManager undo/redo + drag coalescing (when built with JUCE)
docs/               Product, DSP, wireframes
```

Libraries: `LooperAutoMapper` (filename map), `LooperDsp` (Hermite/AmpEnv/SamplePool/VoiceEngine/SVF, **no JUCE**), `LooperPatch` (JSON patch IO + SessionPrefs + SampleRelocator, **no JUCE**), `LooperZoneEdit` (zone editor rules, **no JUCE**). Plugin links all four.

### Automatic pitch detection

Every imported sample is analysed on import (cached per sample) with
**YIN** (`Source/AutoMapper/PitchDetector.*`, plain C++, no JUCE):

- Mix to mono, remove DC, find the onset, skip ~70 ms of attack, then run YIN on up to 9
  frames spread across the next ~1.5 s (frames that decayed > 40 dB are ignored).
- Per frame: difference function (FFT autocorrelation) → cumulative mean normalised
  difference → first dip below 0.12 (smallest lag, avoids octave-low errors) → octave-high
  guard → parabolic interpolation. Range 27.5 Hz–4.2 kHz (A0–C8).
- Median f0 across frames → nearest MIDI note (C4 = 60, A4 = 440 Hz) + cents offset.
  Confidence = periodicity × frame agreement; < 0.5 → **unpitched** (drums/noise/silence).
- A filename note always wins. For samples **without** one, detected roots set the zone root;
  `Zone::tuneCents` gets −cents so detuned samples play in tune. Review map shows e.g.
  `Detected C#3 (−12 ct) 92%`, and warns below 80 % confidence.
- **Filename/audio mismatch:** for samples **with** a filename note, a confident detection
  (≥ 0.8) that lands on a different note (≥ 1 semitone) adds a Review warning such as
  `Filename C4, audio sounds E4` or `Filename C5, audio sounds an octave lower (C4)`. The
  filename still wins; the row's **Use detected** button re-maps that one sample to the
  detected note (+ fine-tune) before you accept.
- **No clear pitch** (no filename note, audio unpitched) — Settings → Mapping:
  - *Spread chromatically* (default): drum-kit layout. Each sound gets its own single key,
    consecutive from the *Unpitched start key* (default MIDI 36, the GM kick — shown as C2 with
    C4 = 60, C1 with C3 = 60), in natural filename order (`Tom 2` before `Tom 10`), skipping
    keys already used as pitched roots. Velocity layers / RR alternates of one sound
    (`Snare_rr1`, `Snare_rr2`, `Kick_soft`, `Kick_hard`) share a key. Pitched zones' key spans
    are clipped around the drum block. Review shows an **Unpitched** tag and the assigned key.
  - *Fixed root C4, full range*: the previous "equal spread + warn" behaviour (root middle C,
    span shared with pitched roots, warning per sample).
- **Middle C convention** (Settings → Mapping): C4 = 60 (default) or C3 = 60. With C3 = 60,
  filename notes parse one octave higher (`Piano_C3.wav` → MIDI 60) and Review / Settings
  note labels use that naming.
- Patches store per-sample `pitchSource` (`filename` / `detected` / `unpitched`),
  `pitchConfidence`, `detectedCents`, `detectedPitchHz`, `detectedRootKey` and `pitchMismatch`.
  The fields are optional and additive (schema stays 1): older patches load with them unset,
  and older builds ignore them.
- Cost: a few ms per sample (≈17 ms for a 3 s stereo 48 kHz file in an unoptimised build).

### Implemented vs TODO

| Area | Status |
|------|--------|
| Filename → note / vel / RR tokens | **Done** |
| Velocity layer midpoints, key midpoints, RR groups | **Done** |
| Duplicate warnings, deterministic maps | **Done** |
| YIN pitch detect (no note in filename) | **Done** — root key + fine-tune cents + confidence; unpitched → chromatic drum keys (or legacy fixed root) |
| Filename vs audio mismatch flag + Use detected | **Done** |
| C3 = 60 naming option | **Done** (filename parse + labels) |
| Hermite resampling + voice steal | **Done** |
| Demo RAM sample + single full-range zone | **Done** |
| Linear AmpEnv ADSR + APVTS params | **Done** |
| SVF (LP/HP/BP) + cutoff / res / env amt APVTS | **Done** |
| Map RR / velocity layers at play | **Done** (RR Cycle or Random per patch; vel layers by range) |
| Streaming sample I/O | TODO |
| Patch JSON save/load + host state | **Done** |
| WAV/AIFF/FLAC RAM load on import | **Done** |
| Drag-drop import → AutoMapper → Review → Accept | **Done** |
| MainView empty/loaded + ReviewMapView table | **Done** |
| Settings (Engine / Mapping / MIDI / Files / About) | **Done** |
| Keyboard strip graphic | **Done** — zones, root dots, selection; click to select / audition |
| Manual zone editor (root, keys, velocity, tune, gain, RR) + undo/redo | **Done** — see below |
| Relocate missing samples (search folder / locate / cascade) | **Done** — see below |

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
5. After Accept, the main view shows zone/root counts, the **ZONES** list + zone editor, **Review map** (re-open last result), and **+ Samples**.

Host-automatable params: **Attack / Decay / Sustain / Release / Volume / Filter Type / Cutoff / Resonance / Filter Env Amt / Round Robin**.

### Round-robin mode (Cycle / Random)

Zones that share a note, a velocity layer and a non-zero `rrGroup` are round-robin alternates. The **ROUND ROBIN `Cycle | Random`** switch sits in the header of the **ZONES** card on the main view (also *Settings → Mapping → Round-robin mode*, and the host parameter **Round Robin**):

- **Cycle** (default, unchanged v1 behaviour): alternates play in `rrIndex` order, wrapping around; one counter per `rrGroup`.
- **Random**: a uniformly random alternate each hit, **never the same one twice in a row** when the group has 2+ alternates (a single alternate just plays). The "last played" memory is per keyzone / velocity-layer group.
- Real-time safe: PCG32 generator (`Source/VoiceEngine/FastRng.h`), fixed-size per-group state table and stack scratch, so zone selection never allocates or locks on the audio thread (verified by `RoundRobinTests` with a counting `operator new`). Each plugin instance seeds itself differently; tests seed explicitly.
- Saved per patch as `map.roundRobinMode` (`"cycle"` / `"random"`) in `.looper.json` and in host state. Older patches without the field load as **Cycle**. The switch is dimmed (still usable) when the patch has no RR alternates.

### Edit zones by hand (main view)

After an import is accepted (or a patch is opened) the main view's lower card is split: the **ZONES** list on the left (one row per zone: sample, root, key range, velocity range, RR alternate; a sand dot marks zones edited since auto-map, a sand **!** marks overlaps) and the **zone editor** on the right. Select a zone by clicking a row, or by clicking a key inside the zone on the **keyboard strip** above (repeated clicks on a key cycle through stacked zones; the selected zone is outlined in sand and every zone's root key carries a dot).

- **Fields:** Root key, Fine tune (cents), Gain (dB), Low/High key, RR group (0 = off), Low/High velocity, RR alternate. Drag a bar (relative, a click never jumps), use the mouse wheel, or click to type (notes like `E3` or MIDI numbers). Note labels follow *Settings → Mapping → Middle C* (C4 = 60 / C3 = 60).
- **Validation:** keys and root clamp to 0–127, velocities to 1–127, fine tune to ±100 ct, gain to −48…+24 dB. Ranges never invert: dragging a low edge past the high edge pushes the high edge along (and vice versa). Overlaps with other zones are **warned, not blocked** ("! Overlaps 1 zone: Pluck.wav · first match plays"); round-robin alternates in the same group are not reported.
- **Use detected pitch:** when the sample has a stored detection, sets root to the detected note and fine tune to −(detected cents), e.g. a pluck 15 ct sharp on E3 → root E3, −15.0 ct. Disabled when already in use or no detection exists (drums, old patches).
- **Reset to auto:** restores every editable field to what AutoMapper chose at import. Zones store that snapshot as `"auto": {…}` in `.looper.json`; patches saved before the zone editor have no snapshot, so the button stays disabled for them.
- **Audition:** click a zone's root key on the strip (dot), or hold **Audition**, to hear exactly that zone (root key clamped into its key range, velocity 100 clamped into its layer).
- **Undo / Redo:** **Cmd/Ctrl+Z**, **Shift+Cmd/Ctrl+Z** (or Ctrl+Y), or the editor's Undo/Redo buttons (`juce::UndoManager`, ~200 steps). A whole slider drag is one undo step. History is cleared when a new import is accepted or a patch is loaded.
- **Live + saved:** edits apply to playback immediately — the edited map is handed to the audio thread lock-free (try-lock at block start; the old map is freed on the message thread) and voices already sounding that zone follow root / fine tune / gain changes; key, velocity and RR changes apply from the next note. Every edit marks the patch **unsaved**; **Save** writes the zones to `.looper.json`.

Rules live in `Source/ZoneEdit/ZoneEditor.*` (no JUCE) and are covered by `ZoneEditorTests`; see `docs/zone-editor.md`.

### Save / Load patch (Standalone or plugin UI)

1. Import + **Accept map** so you have a user instrument (Save is enabled).
2. Click **Save** → choose `Something.looper.json` (or `.json`). Sample paths are stored **relative to the patch file’s directory** when possible; audio is **not** embedded.
3. Click **Open…** (available even on the empty drop screen) → pick a `.looper.json` / `.json`. Samples are decoded on the message thread into `SamplePool`; if any sample files are missing the patch still loads (those zones stay **silent**) and the **Relocate** screen opens.
4. Host project save/recall (`getStateInformation` / `setStateInformation`) embeds APVTS + a patch JSON blob. If sample paths from the previous session are still valid, buffers reload; otherwise the zones stay silent and the main view shows a **“N samples missing · Relocate…”** banner.

**Format sketch (schemaVersion 1):** `name`, `patchRoot`, `samples[]` (`id`, `path`, …), `map` (globals incl. optional `roundRobinMode` + `zones[]`, each zone with an optional `auto` snapshot for Reset to auto), optional `params` (APVTS float snapshot).

### Relocate missing samples

When sample files have moved, Looper still loads the patch (offline zones are silent) and lists the missing files on the **Relocate** screen (sample, original path, status). Open it from the sand **“N samples missing · Relocate…”** banner on the main view.

- **Search folder…** — pick a folder; it is scanned recursively (background thread) and every missing file is matched by **file name, case-insensitive**. If several files share the name, the one whose trailing sub-path best matches the original (e.g. `Grand Piano/Soft/C4.wav`) wins; a tie is relinked to a deterministic best guess and marked **ambiguous** (sand; hover for the other candidates). Ties are broken by sibling consensus when other samples from the same original folder were found unambiguously.
- **Locate…** (or double-click a row) — pick one file by hand (any name). Looper then tries the rest **from the same place**: it mirrors the original folder structure around the located file (same folder, sibling folders, renamed parents), then searches that folder recursively.
- **Skip** leaves a sample offline; **Close/Done** returns to play.

Each found file is decoded and swapped in **live** (next note-on plays it) and the patch is marked **unsaved**; the next **Save** writes the new paths relative to the patch file. The matching logic lives in `Source/PatchStore/SampleRelocator.*` (std::filesystem, no JUCE) and is covered by `RelocatorTests`.

**Limitations (v1):** no sample embedding; streaming long files still TODO. Relocate: case folding covers ASCII + Latin-1 letters only (no full Unicode case/normalization — e.g. NFD vs NFC names won't match); scans stop after 250 000 files / depth 32; a cascade/search never overrides a file you already relinked unless it was ambiguous.


## Build

JUCE is pulled via **CMake FetchContent** (not vendored). Requires CMake ≥ 3.22 and a C++20 compiler.

### DSP + AutoMapper unit tests (any OS, no GUI deps)

```bash
cmake -S . -B build -DLOOPER_BUILD_PLUGIN=OFF -DLOOPER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
# or: ./build/AutoMapperTests && ./build/PitchDetectorTests && ./build/DspVoiceTests && ./build/PatchStoreTests && ./build/SessionPrefsTests && ./build/RelocatorTests && ./build/RoundRobinTests && ./build/PitchMappingTests && ./build/SourceEncodingTests && ./build/ZoneEditorTests
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

Open the **gear** button (top-right on the main view) in Standalone or the plugin editor. **← Back to play** returns to MainView.

| Tab | Controls |
|-----|----------|
| **Engine** | Polyphony (1–128) → map + voice engine; Interpolation (Hermite; sinc later); Glide ms (stored on map; engine portamento TODO); Master soft-clip On/Off (tanh on voice sum); Default filter LP/HP/BP → APVTS |
| **Mapping** | Middle C C4 = 60 (default) / C3 = 60; Key span full 0–127 vs natural; Round-robin mode Cycle / Random (per patch, same parameter as the main-view switch); Velocity curve Linear/Soft/Hard; No clear pitch: Spread chromatically (default) / Fixed root C4, full range (legacy); Unpitched start key (default MIDI 36); Open review after import On/Off |
| **MIDI** | Pitch bend ±2 (stored; engine uses range); Mod target FilterCutoff / Volume; Sustain CC64 note (TODO); Clear MIDI learn stub |
| **Files** | Last patch path; missing-file policy (silent zones + Relocate…); Reveal last patch folder |
| **About** | Looper / Loop Audio Lab / version / GitHub URL |

Session prefs persist in host state (`prefsJson` alongside patch). Mapping options feed the **next** AutoMapper import. ADSR / filter knobs stay on MainView.

### UI text and symbols (no mojibake)

`juce::String (const char*)` reads its argument as ASCII/Latin-1, so a raw UTF-8 literal such as `"Off · 0 ms"` shows up garbled, and MSVC can mangle it at compile time. Rule: **`Source/UI` and `Source/Plugin` stay pure ASCII**; every middle dot, ellipsis, dash, arrow or ± comes from `Source/UI/Glyphs.h` (`glyph::ellipsis()`, `glyph::dotSep()`, … built with `CharPointer_UTF8` from escaped bytes), and the settings gear is a drawn vector icon (`GearButton`), not a font glyph. `SourceEncodingTests` fails the build's test step if a raw non-ASCII literal (anywhere in `Source/`) or a stray UTF-8 escape (UI/Plugin outside `Glyphs.h`) appears.

## Status — drag-drop + review UI

**Done:** drop/browse import → AutoMapper → Review map → Accept into playable map; APVTS knobs; thread-safe map swap via `shared_ptr`/`adoptMap`; Settings screen with session prefs.

**Polish TODOs:** glide/portamento DSP polish, sustain pedal, MIDI learn, zone drag-resizing on the strip.

## Next milestone

Streaming sample I/O → optional dedicated filter envelope.

## Contributing / next steps

1. Zone editor: drag zone edges / roots directly on the keyboard strip; multi-zone selection; zone gain smoothing.
2. Per-row manual root-key editing in Review (beyond "Use detected"); the main-view zone editor covers it after Accept.
3. Relocate: optional file-hash verification of candidates.
4. Parameter smoothing on continuous filter/env params; finish glide DSP.
5. Choose LICENSE compatible with your JUCE license (GPL vs commercial).

---

## Windows VST3 (CI)

Every push to `main` runs GitHub Actions on `windows-latest` and uploads a **Windows x64 VST3** artifact named `Looper-windows-x64-vst3`.

1. Open the [Actions](https://github.com/TheLoopGolf/Looper/actions) tab
2. Open the latest **Build Windows VST3** run
3. Download **Looper-windows-x64-vst3**
4. Unzip and copy `Looper.vst3` into your DAW’s VST3 folder (often `C:\Program Files\Common Files\VST3`), then rescan

You can also trigger a build manually: Actions → Build Windows VST3 → Run workflow.

Linux VST3 / Standalone are built on the Loop Audio Lab machine; macOS AU+VST3 are built by the macOS workflow below.

## macOS AU / VST3 (CI)

Every push to `main` also runs GitHub Actions on `macos-latest` and uploads a **universal (Apple Silicon + Intel, macOS 11.0+)** artifact named `Looper-macos-universal`. It contains `Looper-macos-universal-vst3.zip`, `Looper-macos-universal-au.zip`, `Looper-macos-universal-standalone.zip` and `INSTALL.txt`. The workflow runs every `*Tests` suite, ad-hoc signs the bundles and runs `auval -v aumu LoAp Loop`.

1. Open the [Actions](https://github.com/TheLoopGolf/Looper/actions) tab
2. Open the latest **Build macOS AU + VST3** run
3. Download **Looper-macos-universal** and unzip the zips you need
4. Copy the plug-ins and clear the quarantine flag. The builds are ad-hoc signed only, not notarized:

```bash
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R Looper.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R Looper.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Looper.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Looper.component
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

5. Rescan plug-ins or restart your DAW. In Logic Pro, open Settings → Plug-in Manager, select Looper and choose **Reset & Rescan Selection**.

You can also trigger a build manually: Actions → Build macOS AU + VST3 → Run workflow.

