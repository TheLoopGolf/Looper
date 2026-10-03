# Sound shaping: filter types, filter envelope, key tracking, velocity, bend range

This phase adds per-patch voice shaping to the main view's bottom **sound deck**. Everything here
is a host-automatable APVTS parameter, saved in patches (`params`) and in plugin/session state.
Old patches and sessions play **bit-identically** to v1 (proven by `SoundShapingTests`, which
renders against a frozen copy of the v1 engine in `Tests/reference/v1/`).

## Main view layout (1000 x 680)

| Row | Section | Controls |
|-----|---------|----------|
| 1 | AMP | VOL, ATK, DEC, SUS, REL (unchanged) |
| 1 | FILTER | TYPE segmented control **LP12 / LP24 / BP / HP**, CUT, RES, KEY (key tracking) |
| 2 | VELOCITY (brass) | AMP, CUT (bipolar), ATK |
| 2 | FILTER ENV (brass) | ATK, DEC, SUS, REL, AMT (bipolar) |
| 2 | BEND | UP, DOWN (semitones) |

Bipolar knobs fill from 12 o'clock. Double-click any knob to reset it; hover for a tooltip. Values
show units ("850 ms", "1.20 s", "+12 st", "2.40 kHz", "80%") and accept typed values in the same
form. **Settings → MIDI → Pitch bend range** offers presets (±1 … ±48) that set both directions;
an asymmetric patch shows as e.g. "+12 / −2 semitones". **Settings → Engine → Default filter type**
now includes *Low-pass 24 dB*.

## Signal path

Per voice, per sample:

```
octaves = legacyAmt * ampEnv                    (v1 "filterEnvAmt", octaves; legacy)
        + modWheel                               (v1 mod wheel -> cutoff, unchanged)
        + fenvAmount/12 * filterEnv              (new, semitones -> octaves)
        + keyTrack * (note - 60) / 12            (new, pivot at middle C / MIDI 60)
        + velCutoff/12 * (curvedVel - 1)         (new, 0 at velocity 127)
cutoffHz = cutoff * 2^octaves, clamped to 20 Hz .. 0.49 * sampleRate
```

`curvedVel` is velocity/127 after the map's velocity curve (Linear / Soft / Hard).

- **Filter types:** LP12 / HP / BP are the v1 Cytomic-style TPT SVF (12 dB/oct). **LP24** is the
  same resonant stage followed by a second, Butterworth-damped (Q = 0.707) low-pass stage at the
  same cutoff, giving 24 dB/oct with the resonance peak of the first stage.
- **Filter envelope:** its own linear ADSR (same envelope class as the amp envelope) that is
  triggered and released with the amp envelope (sustain pedal included).
- **Velocity → amp:** gain = 1 − amount × (1 − curvedVel). 100% is v1 (gain = curvedVel), 0% plays
  every note at full level.
- **Velocity → attack:** attack × 2^(4 × amount × (1 − curvedVel)). Velocity 127 always uses the
  knob's attack; at 100% the softest note's attack is up to 16× longer. Amp envelope only.
- **Bend:** up range applies to positive bend, down range to negative. Changing a range while the
  wheel is held re-applies the bend.

## Smoothing and real-time safety

Cutoff (in log2 space), resonance, filter-env amount, key tracking and velocity → cutoff ramp
linearly over **20 ms** (`ParamRamp`, the last step lands exactly on the target) and are evaluated
per sample, so sweeps have no zipper noise. When no voice is sounding, the values snap. Per-note
values (velocity amounts, attack, key offset) are taken at note-on. No allocation or locks on the
audio thread (checked by an allocation counter in the tests). Filter states are flushed to zero
when they become subnormal, and the filter clamps cutoff and resonance so it stays stable at
every extreme combination the tests try (22.05–192 kHz, all types, maximum resonance,
±60 st filter-env and velocity modulation).

## Parameters

| id | name | range | default (= v1 sound) |
|----|------|-------|-----------------|
| `filterType` | Filter Type | 0 LP12, 1 HP, 2 BP, **3 LP24** | 0 |
| `filterEnvAmt` | Amp Env > Cutoff (legacy) | −1 … +1 oct | 0 |
| `fenvAttack` | Filter Env Attack | 0.1 … 5000 ms | 1 |
| `fenvDecay` | Filter Env Decay | 1 … 5000 ms | 400 |
| `fenvSustain` | Filter Env Sustain | 0 … 1 | 0 |
| `fenvRelease` | Filter Env Release | 1 … 8000 ms | 300 |
| `fenvAmount` | Filter Env Amount | −60 … +60 st | 0 |
| `keyTrack` | Key Tracking | 0 … 100 % | 0 |
| `velAmp` | Velocity > Amp | 0 … 100 % | 100 |
| `velCutoff` | Velocity > Cutoff | −60 … +60 st | 0 |
| `velAttack` | Velocity > Attack | 0 … 100 % | 0 |
| `bendUp` / `bendDown` | Bend Up / Down | 0 … 48 st | session bend range (2) |

The new ids use `ParameterID` version 2. The list of ids saved in patches is
`looper::sound::kPatchParamIds` (`Source/SoundShaping/SoundParams.h`, no JUCE dependency); the
same header converts raw parameter values into engine settings (`fromRawValues`,
`applyToEngine`), so the tests drive exactly the code the plugin runs.

## Old patches and sessions

- A patch or host session saved before this phase has none of the new ids. On load each one is
  reset to the default in the table above (bend → the session's pitch-bend range), which
  reproduces the v1 sound exactly, including when the previous patch had changed them.
- v1's single "Filter Env" knob was really **amp envelope → cutoff**. It is kept as the hidden
  legacy parameter `filterEnvAmt` so old patches and automation sound the same. When it is
  non-zero the main view shows a sand chip **"Amp env → cutoff +x oct · Move to filter env"**.
  Clicking it copies the amp ADSR into FILTER ENV with the same depth (× 12 st) and switches the
  legacy modulation off (the same sound, now editable). If the filter envelope is already in use,
  the chip offers **Remove** instead.
- Fixed: v1 re-applied **Settings → Default filter type** whenever session prefs were applied,
  which could override a restored session's own filter type. It now applies only when you change
  it in Settings.

## Tests (`SoundShapingTests`)

Filter magnitude per type against the analog prototype at the pre-warped frequency (±0.25 dB) plus
slopes / band-pass shape; LP12/HP/BP bit-identical to v1; filter envelope sweeps the cutoff as set;
key-tracking math; velocity curves (amp, cutoff, attack, measured on renders); bend up/down;
ramp timing / monotonicity / exact landing and a second-difference discontinuity metric (smoothed
sweep vs v1 step); stability and no subnormals at extreme settings; no allocation in `process`;
four old-patch scenarios rendered through the frozen v1 engine and the new path, bit-identical;
patch parameter round trip.

## Limitations

- Volume changes are still applied per block (unchanged, to stay bit-exact with v1).
- Switching filter type is not crossfaded; LP24's second stage starts from silence.
- Changing a sustain level while a note is held still steps (amp and filter envelopes).
- Velocity amounts and the key offset are fixed at note-on; velocity → cutoff is relative to full
  velocity (velocity 127 = no offset); key tracking pivots at MIDI 60.
- Velocity → attack only affects the amp envelope, not the filter envelope.
- Host automation recorded on `filterType` with an older build may land on a different type,
  because the parameter now has 4 choices (its normalised scale changed). Saved patches and
  sessions store the index and are unaffected.
- Not yet tested inside a real DAW.
