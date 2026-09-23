# Looper — Product brief (locked)

**Working name:** Looper (Loop Audio Lab) — crowded name; treat as working title pending trademark check.

**Purpose:** Multi-sample instrument builder for keys-playing producers. Drag in WAVs/folders, get a playable instrument in minutes with keyzones, velocity layers, and round-robins — without Kontakt complexity.

**Target user:** Producers who play keys; DAW-comfortable; want their own samples playable and expressive without a heavy sample engine.

**Content model:** Simple builder — user samples; mapping mostly automatic.

**v1 DSP:** Streaming sample engine, polyphony + steal, keyzones + velocity layers + cycling RR, ADSR amp, one multimode SVF filter (env from amp ADSR), pitch bend / mod wheel (one destination), Hermite resampling. No granular, timestretch, or scripting.

**Formats:** VST3 + AU, macOS + Windows. JUCE 8, C++20. Patch = JSON sidecar + relative sample paths.

**UI (locked):** Play is home; mapping is a review pass. Screens: main empty, main loaded, review map, settings (Engine / Mapping / MIDI / Files / About). Dark, calm studio UI.

**Defaults:** C4=60, full 0–127 key span, RR cycle, glide off, soft-clip off.

See also: `dsp-and-automapper-v1.md`, `ui-wireframe-notes.md`, `wireframes/`.
