# Looper v1 — DSP & AutoMapper Design

Working name: **Looper**  
Product: multi-sample instrument builder for keys players  
Stack: JUCE 8, C++20, VST3 + AU, macOS + Windows

---

## 1. Goals & non-goals

### Goals
- Drag-in WAVs → playable mapped instrument in minutes
- Correct, predictable auto-mapping with a one-glance review/fix pass
- Clean, low-CPU polyphonic playback with expressive basics (velocity, RR, ADSR, one filter)
- Modular code ready for later layers (FX, library packaging, MPE)

### Non-goals (v1)
- Granular, timestretch, pitch-shift quality beyond resampling
- Scripting / Kontakt-like language
- Convolution, complex FX chain
- Full MPE / per-note controllers
- Embedded sample packaging (relative paths only in v1)

---

## 2. Data model

### SampleRef
- `id` (UUID)
- `path` (relative to patch root preferred; absolute allowed)
- `fileHash` (optional, for relocate hints)
- `durationSamples`, `sampleRate`, `channels` (1 or 2; >2 downmix on load)
- `detectedPitchHz` / `detectedRootKey` (nullable)
- `loopStart` / `loopEnd` (nullable; v1: one-shot by default, optional sustain loop if markers exist later)

### Zone
- `sampleId`
- `rootKey` (0–127)
- `keyLow`, `keyHigh`
- `velLow`, `velHigh` (1–127; 0 reserved)
- `rrGroup` (int, default 0 = no RR)
- `rrIndex` (int within group)
- `tuneCents` (−100..+100 fine; coarse transpose separate)
- `coarseTranspose` (semitones)
- `gainDb`, `pan` (−1..1)
- `sampleStart`, `sampleEnd` (optional trim)

### InstrumentMap
- Ordered list of `Zone`
- Global: `volumeDb`, `polyphonyLimit`, `glideMs`, `velCurve` (enum: linear / soft / hard)
- Mod routing: `modWheelTarget` = `FilterCutoff` | `Volume` (v1: one destination)

### Voice (runtime)
- Active zone + sample reader
- Pitch ratio, phase/read position
- Amp env state, filter state
- Note number, velocity, channel, age (for stealing)
- Release flag / sustain-pedal held

### Patch file (JSON sidecar)
- Schema version
- Map + global params
- Sample list with relative paths
- UI hints (last zoom on keyboard strip)

---

## 3. AutoMapper design

### Pipeline
1. **Ingest** — accept files or folders; recurse audio (`wav`, `aiff`, `flac` if JUCE decode allows); ignore non-audio
2. **Normalize names** — strip extension; Unicode NFKC; lowercase for matching; keep original for display
3. **Tokenize** — extract pitch, velocity, RR, and layer hints
4. **Resolve pitch** — filename wins; else detect; else leave unpitched for equal-spread
5. **Cluster** — group into instruments layers by root + velocity + RR
6. **Build zones** — assign key ranges and velocity ranges without gaps where possible
7. **Emit review model** — confidence per sample + warnings
8. **Commit** — user accepts or edits, then write `InstrumentMap`

### Filename grammar (priority order)

Pitch tokens (first match wins):
- Note name: `\b([a-g])([#b]?)(-?[0-9]|10|11)\b` → MIDI note (scientific; middle C = C4 = 60 by default, document this)
- MIDI number: `\b(12[0-7]|1[01][0-9]|[1-9][0-9]?)\b` when clearly pitch context (`key`, `note`, `n`, or isolated 2–3 digit in common sample packs)
- Prefer explicit note names over bare numbers when both appear

Velocity tokens:
- `vel(ocity)?[_-]?(\d{1,3})`
- `v[_-]?(\d{1,3})` when 1–127
- Words: `ppp|pp|p|mp|mf|f|ff|fff` → fixed table (16, 32, 48, 64, 80, 96, 112, 127)
- `soft|med(ium)?|hard|loud` → 32 / 64 / 96 / 112

Round-robin:
- `rr[_-]?(\d+)`
- `round[_-]?(\d+)`
- `alt[_-]?(\d+)`
- Bare `_01`, `_02` only if same root already resolved and multiple files share that root with sequential suffixes

Ignore noise tokens: `wav`, `sample`, `mapped`, `normalized`, `48k`, `24b`, `loop`, date stamps.

### Pitch detection fallback
- Run only when no reliable filename pitch
- Algorithm: **YIN** (or MCM for speed) on a center window of the sample (avoid attack transient bias where possible; for one-shots use first 50–200 ms after onset)
- Onset: simple energy threshold or first sample above −40 dBFS
- Accept if confidence high (periodicity / YIN probability above threshold); else mark `unpitched`
- Quantize to nearest MIDI note; store cents offset into `tuneCents` suggestion (user can snap to 0)

### Zone building algorithm

**Same root, multiple velocities** → velocity layers:
1. Collect unique roots
2. For each root, sort samples by resolved velocity value (or by order if missing)
3. Split 1–127 into N contiguous ranges (boundaries at midpoints between sorted velocities)
4. Single velocity sample → full 1–127

**Same root + velocity + RR** → round-robin group:
- All zones share key/vel range; differ by `rrIndex`
- Playback picks next index (cycle) or random (global preference; default cycle)

**Key range for different roots**:
1. Sort unique roots ascending
2. Boundary between root A and B at midpoint (floor)
3. Lowest root extends to 0 (or configurable floor, default 0); highest to 127
4. Optional: clamp unused extremes if roots only cover a region and user prefers “natural span only” (v1 default: full keyboard coverage so every key plays something)

**Conflicts**:
- Duplicate identical (root, vel, rr) → keep louder peak / longer file; warn duplicate
- Overlapping ambiguous tokens → prefer note-name pitch; flag warning
- Folder drop with mixed instruments (huge pitch span + unrelated names) → still map, but warn “possible multi-instrument folder”

### Review model (UI contract)
Per sample:
- `source` = `filename` | `detected` | `spread`
- `confidence` 0–1
- `warning` strings (duplicate, low confidence, stereo/mono mix, SR mismatch)

Global summary:
- zone count, layer counts per root, RR group sizes
- sample rate mix warning (engine resamples to host rate per voice; prefer consistent library SR)

### AutoMapper API (module boundary)
```text
AutoMapResult AutoMapper::map(const std::vector<SampleRef>& samples, AutoMapOptions opt);
```
Options: middle-C convention (C3 vs C4), prefer full-keyboard span, RR mode default, velocity word table.

Deterministic: same inputs → same map (required for tests).

### Test vectors (must-have)
- `Piano_C4_v32.wav`, `Piano_C4_v96.wav`, `Piano_D4_v64.wav`
- `kick_c1.wav` (single)
- `snare_rr1.wav` … `snare_rr4.wav` same implied root
- `Pad_soft_A3.wav`, `Pad_hard_A3.wav`
- Garbage names → pitch detect path
- Empty list → empty map, no crash

---

## 4. DSP architecture

### Threading
- **Audio thread**: voice render only; lock-free parameter snapshots; no file I/O, no alloc in steady state
- **Message thread**: load samples, AutoMapper, patch parse, UI
- Sample data published via reference-counted immutable buffers / streaming readers swapped atomically

### Sample reading & streaming
- Short samples (< ~5 s mono @ 48 k) may fully RAM-cache
- Longer: streaming with pre-buffer; voice holds read cursor
- Resample to host sample rate with quality tier:
  - **v1 default**: 4-point Hermite (good CPU/quality)
  - Optional higher quality later (windowed sinc)
- Mono → stereo duplicate; stereo → stereo; multi-channel → downmix to stereo on load

### Pitch ratio
```text
ratio = 2^((note + bendSemis + coarse + cents/100 - rootKey) / 12)
```
- Pitch bend: ±2 semitones default (host/synth may override via RPN later)
- Glide: portamento toward target ratio over `glideMs` when legato (optional mode: always / legato-only; v1: off by default, global ms when on)

### Voice engine
- Max voices = user polyphony (default 64; clamp 1–128)
- Voice steal policy:
  1. Prefer releasing/quietest (lowest envelope gain)
  2. Then oldest non-held
  3. Never steal the most recent note if avoidable
- Same note retrigger: kill previous voice on that note/channel (default) or allow overlap (option later)
- Sustain pedal: defer note-offs while pedal down; release on pedal up

### Amp envelope (ADSR)
- Digital ADSR with **parameter smoothing** and optional **exp curves** on A/D/R (slightly analog-feeling without modeling a specific hardware envelope)
- Stages: Attack → Decay → Sustain → Release
- Times in ms; attack from 0; decay to sustain level; release from current level on note-off
- Minimum attack ~0.1 ms to avoid clicks; tiny fade on steal

### Filter
- **Topology**: linear SVF (Chamberlin / Andy Simper style) — stable multimode LP/HP/BP from one structure
- Per-voice filter state (so cutoff modulation is musical under polyphony)
- Parameters: type, cutoff Hz, resonance (Q), env amount (−100%..+100% of cutoff in octaves or Hz mapping — use octave-based for musical feel)
- Filter envelope: reuse amp ADSR shape scaled by env amount **or** a shared simple decay; v1: **reuse amp ADSR** as mod source to save UI complexity (document this; dedicated filter env is roadmap)
- Soft-limit resonance to avoid blow-ups; flush denormals

### Modulation (v1)
- Velocity → amp (always); optional velocity → cutoff amount (global %)
- Mod wheel → FilterCutoff **or** Volume (switch)
- All continuous params smoothed (5–20 ms) to avoid zipper noise

### Gain staging
- Per-zone gain → voice → sum → master volume
- Soft clip / tanh ceiling optional at master (off by default) to catch stacking peaks
- Headroom: sum in float, clip only at end if enabled

### CPU considerations
- Process block in SIMD-friendly loops where JUCE helpers allow
- Skip silent voices (env idle + fade complete)
- Denormal prevention on filter/env states
- Precompute zone lookup: for note+vel, pick candidate zones quickly
  - Structure: array of zones; for each note, small list of matching zones (rebuild on map change)
  - RR counter per group

### Zone lookup (note on)
1. Find zones where `keyLow ≤ note ≤ keyHigh` and `velLow ≤ vel ≤ velHigh`
2. If multiple RR indices in same group, pick next RR
3. If still multiple (bad map), pick highest confidence / first stable order
4. Start voice with that zone

---

## 5. Parameter list (host-automatable)

| Param | Range | Notes |
|-------|-------|-------|
| Volume | −60..+12 dB | Master |
| Pan | −1..1 | Global |
| Polyphony | 1..128 | Not necessarily continuous automation-critical |
| Glide | 0..2000 ms | |
| Attack | 0.1..5000 ms | |
| Decay | 1..5000 ms | |
| Sustain | 0..1 | |
| Release | 1..8000 ms | |
| Filter type | LP/HP/BP | Choice |
| Cutoff | 20..20000 Hz | Log |
| Resonance | 0..1 | Mapped to Q |
| Filter env amt | −1..1 | |
| Mod target | Filter/Volume | Choice |
| Vel→filter | 0..1 | |

Per-zone params are **not** all host automatable in v1 (edited in map UI, stored in patch).

---

## 6. Edge cases & correctness

- Sample file missing on load → mark offline; keep zone; UI “locate…”
- Host SR change mid-flight → update ratios; streaming reopens as needed
- Offline bounce / huge blocks → engine must be block-size agnostic
- 0-length or corrupt file → skip with warning in AutoMapper
- Mono/stereo mixed libraries → OK
- Extremely short clicks (< 64 samples) → still play; ensure env doesn’t click harder than sample

---

## 7. Implementation order (within scaffolding)

1. Voice + single sample + Hermite + ADSR
2. Filter SVF on voice
3. InstrumentMap + note lookup + polyphony/steal
4. Velocity layers + RR counters
5. AutoMapper + tests (filename table first, then YIN path)
6. Streaming + patch IO
7. Parameter smoothing + mod wheel

---

## 8. Open decisions (defaults locked unless product overrides)

| Topic | v1 default |
|-------|------------|
| Middle C convention | C4 = 60 |
| Key span | Full 0–127 |
| RR mode | Cycle |
| Filter env source | Amp ADSR |
| Interpolation | 4-point Hermite |
| Glide | Off |
| Soft clip master | Off |
| Sample packaging | Relative paths |

