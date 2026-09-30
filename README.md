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
  AutoMapper/       Filename parse + zone building + YIN pitch detection (PitchDetector)
  InstrumentMap/    Zone, SampleRef, InstrumentMap types
  SamplePool/       RAM SampleBuffer pool + demo tone generator
  VoiceEngine/      Polyphony, steal, Hermite + AmpEnv + SVF; vel layers + RR cycle
  AmpEnv/           Linear ADSR (min attack 0.1 ms)
  Filter/           Linear Simper SVF (LP/HP/BP, dual-state stereo)
  MidiRouter/       Note/CC + pitch bend (±2 st); CC1 → cutoff when target FilterCutoff
  PatchStore/       JSON sidecar (.looper.json) — relative sample paths, schema v1;
                    SampleRelocator (find moved/missing samples, no JUCE)
  UI/               MainView (drop/loaded), ReviewMapView (table), SettingsView, RelocateView
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
docs/               Product, DSP, wireframes
```

Libraries: `LooperAutoMapper` (filename map), `LooperDsp` (Hermite/AmpEnv/SamplePool/VoiceEngine/SVF, **no JUCE**), `LooperPatch` (JSON patch IO + SessionPrefs + SampleRelocator, **no JUCE**). Plugin links all three.

### Automatic pitch detection

Samples whose filename has no note name (e.g. `Pluck_alpha.wav`) are analysed on import with
**YIN** (`Source/AutoMapper/PitchDetector.*`, plain C++, no JUCE):

- Mix to mono, remove DC, find the onset, skip ~70 ms of attack, then run YIN on up to 9
  frames spread across the next ~1.5 s (frames that decayed > 40 dB are ignored).
- Per frame: difference function (FFT autocorrelation) → cumulative mean normalised
  difference → first dip below 0.12 (smallest lag, avoids octave-low errors) → octave-high
  guard → parabolic interpolation. Range 27.5 Hz–4.2 kHz (A0–C8).
- Median f0 across frames → nearest MIDI note (C4 = 60, A4 = 440 Hz) + cents offset.
  Confidence = periodicity × frame agreement; < 0.5 → **unpitched** (drums/noise/silence).
- A filename note always wins. Detected roots set the zone root; `Zone::tuneCents` gets
  −cents so detuned samples play in tune. Unpitched samples use the Settings → Mapping
  *unpitched fallback* (equal spread + warn). Review map shows e.g.
  `Detected C#3 (−12 ct) 92%`, and warns below 80 % confidence.
- Cost: a few ms per sample (≈17 ms for a 3 s stereo 48 kHz file in an unoptimised build).

### Implemented vs TODO

| Area | Status |
|------|--------|
| Filename → note / vel / RR tokens | **Done** |
| Velocity layer midpoints, key midpoints, RR groups | **Done** |
| Duplicate warnings, deterministic maps | **Done** |
| YIN pitch detect (no note in filename) | **Done** — root key + fine-tune cents + confidence; unpitched → spread fallback |
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
5. After Accept, the main view shows zone/root counts, sample list, **Review map** (re-open last result), and **+ Samples**.

Host-automatable params: **Attack / Decay / Sustain / Release / Volume / Filter Type / Cutoff / Resonance / Filter Env Amt**.

### Save / Load patch (Standalone or plugin UI)

1. Import + **Accept map** so you have a user instrument (Save is enabled).
2. Click **Save** → choose `Something.looper.json` (or `.json`). Sample paths are stored **relative to the patch file’s directory** when possible; audio is **not** embedded.
3. Click **Open…** (available even on the empty drop screen) → pick a `.looper.json` / `.json`. Samples are decoded on the message thread into `SamplePool`; if any sample files are missing the patch still loads (those zones stay **silent**) and the **Relocate** screen opens.
4. Host project save/recall (`getStateInformation` / `setStateInformation`) embeds APVTS + a patch JSON blob. If sample paths from the previous session are still valid, buffers reload; otherwise the zones stay silent and the main view shows a **“N samples missing · Relocate…”** banner.

**Format sketch (schemaVersion 1):** `name`, `patchRoot`, `samples[]` (`id`, `path`, …), `map` (globals + `zones[]`), optional `params` (APVTS float snapshot).

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
# or: ./build/AutoMapperTests && ./build/PitchDetectorTests && ./build/DspVoiceTests && ./build/PatchStoreTests && ./build/SessionPrefsTests && ./build/RelocatorTests
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

**Polish TODOs:** keyboard strip graphic, glide/portamento DSP, sustain pedal, MIDI learn.

## Next milestone

Streaming sample I/O → optional dedicated filter envelope.

## Contributing / next steps

1. Keyboard strip zone visualization per wireframes.
2. Optional: flag filename-vs-detected pitch mismatches in Review.
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

