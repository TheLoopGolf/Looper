# Manual zone editor

Fixes for anything AutoMapper got wrong, after an import is accepted, without leaving the
main screen.

## Where it lives

Main view, lower card (visible once a user instrument is loaded):

```
+-- keyboard strip ------------------------------------------------------------+
|  zones as tinted spans, root key = dot, selected zones = sand outline + grips |
+------------------------------------------------------------------------------+
+-- ZONES (list) ------------------+-- ZONE  <sample> [. edited] -- Undo Redo --+
| SAMPLE   ROOT  KEYS   VEL   RR   |  ROOT KEY     FINE TUNE     GAIN           |
| . Piano  C4 !  A3-E4  1-127  -   |  LOW KEY      HIGH KEY      RR GROUP       |
|   Pluck  E3    ...               |  LOW VEL      HIGH VEL      RR ALT         |
|                                  |  Detected E3 (+15 ct) . 100% confidence    |
|                                  |  ! Overlaps 1 zone: Pluck.wav . first ...  |
|                                  |  [Use detected pitch] [Reset to auto] [Audition]
|                                  |  SHIFT KEYS [-12] [-1] [+1] [+12]           |
+----------------------------------+--------------------------------------------+
With several zones selected the title reads "N zones selected" and differing fields show
"mixed  C3-C5".
```

* Select: click a list row, or a zone on the strip. Cmd-click (macOS) / Ctrl-click toggles a
  zone, Shift-click selects a range in keyboard (list) order, in the list and on the strip;
  Cmd/Ctrl+A selects all while the list has focus. Repeated plain clicks on one key cycle
  through zones stacked on it. The zone clicked last is the *primary*.
* Audition: click a zone's root key on the strip, or hold **Audition** (primary zone). Plays
  exactly that zone (see "Audition" below).

## Strip editing (Source/ZoneEdit/ZoneStrip.*, no JUCE)

`KeyStripLayout` holds the key geometry (white keys evenly spaced, black keys 62 % wide,
offset 35 % left of the white-key boundary). `hitTestStrip` decides what a mouse-down grabs:
selected zones first (primary first), then the rest in map order; within each group the root
marker (dot +-9 px), then an edge (+-5 px, at most a third of the zone width so narrow zones
keep a body), then the body. A clipped edge (zone extends past the visible range) is not
grabbable.

| Gesture | Part | Result (`computeStripDrag`) |
|---|---|---|
| drag left / right edge | LowEdge / HighEdge | primary's edge = pointer key; other selected zones move that edge by the same delta |
| drag root dot | Root | primary's root = pointer key; others shift root by the same delta |
| drag body | Body | key ranges + roots shift by (pointer key - mouse-down key); one clamp for the group, widths kept |
| Alt/Option-drag | DrawRange | every selected zone gets min(anchor, key)..max(anchor, key) |

* Snap: `nearestKey(x)` = key whose centre is nearest (chromatic), clamped to the visible range.
* Every drag is computed from the map captured at mouse-down, so clamping never accumulates
  and dragging back restores the zones; edges use the editor's `withZoneField` rules (0-127,
  never inverted, partner pushed along).
* The first change of a drag opens an UndoManager transaction, the rest are performed into it
  (and coalesce) -> one undo step named "Drag low key", "Move zones", ...
* Tooltip: `stripDragLabel` -> e.g. `Low F#3 · F#3-E4`, `Root E3 (52)`, `Keys E3-D4 · root A#3 · 3 zones`,
  naming notes with the C4 = 60 / C3 = 60 preference. The strip does not rescale while dragging.
* A drag needs 3 px of movement; a plain click on a member of a multi-selection reduces the
  selection to it on mouse-up (so a group can be dragged without losing it).

## Multi-selection (Source/ZoneEdit/ZoneSelection.*, no JUCE)

* `ZoneSelection`: sorted indices + primary + Shift anchor; `selectOnly`, `toggle`,
  `selectRange(order, i)`, `selectAll`, `set`, `setPrimary`, `prune`.
* `summarizeField` -> mixed flag, min/max, primary's value. The editor shows the primary's
  value on the bar and `mixed  C3-C5` as text.
* `fieldEditsRelative`: root, key edges, fine tune, gain -> `offsetField` (each zone moves by the
  same delta from its own value, clamped individually); velocity edges, RR group / alternate
  -> `setFieldAll` (absolute).
* `classifyMultiEditText`: typed `+N` / `-N` = offset, anything else = absolute, `=-6` forces an
  absolute negative.
* `shiftKeys` / `clampKeyShift`: SHIFT KEYS buttons and group body moves; the shift is clamped so
  every selected range stays inside 0-127.
* Group edits go through `LooperAudioProcessor::performZoneEdits` -> `makeZoneEdits` (unchanged
  zones skipped; a stale index / sample id rejects the whole group) -> one `ZoneEditAction`
  holding every `ZoneEdit` -> `replaceZones` (one map copy, one live hand-off).
* Undo / Redo: Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z, Ctrl+Y, or the buttons. Hosts that capture these
  shortcuts themselves leave the buttons as the fallback.

## Rules (Source/ZoneEdit/ZoneEditor.*, no JUCE)

| Field | Range | Notes |
|---|---|---|
| Root key | 0-127 | may lie outside the key range |
| Low / High key | 0-127 | never inverted: moving one edge past the other drags it along |
| Low / High velocity | 1-127 | 0 is note-off in MIDI; same no-inversion rule |
| Fine tune | -100..+100 ct | stored at 0.01 ct |
| Gain | -48..+24 dB | stored at 0.01 dB |
| RR group | 0-999 | 0 = no round robin |
| RR alternate | 0-127 | order inside the group (Cycle mode) |

* `sanitizeZone` repairs legacy / hand-edited zones (swaps inverted pairs, clamps, NaN -> 0)
  the first time such a zone is edited.
* `findOverlaps` reports zones whose key AND velocity ranges intersect, except alternates in
  the same non-zero RR group. Overlaps are warnings only; the engine plays the first match in
  map order.
* `stampAutoValues` runs when an import is accepted (`commitAutoMapToInstrument`), storing
  AutoMapper's values per zone (`Zone::autoValues`, JSON `"auto"`). "Reset to auto" restores
  them; a zone counts as *edited* when any field differs from its snapshot.
* "Use detected pitch" uses the sample's stored `detectedRootKey` / `detectedCents`:
  root = detected note, fine tune = -cents. Available whenever a detection was stored, even for
  samples whose filename note won at import.

## Undo / redo

Each change is a `ZoneEdit { index, sampleId, before, after }`. The plugin wraps one or a group
of them in `ZoneEditAction` (`juce::UndoableAction`) and performs it on the processor's
`juce::UndoManager`. perform/undo call `LooperAudioProcessor::replaceZones`, which refuses the
action if any zone at those indices no longer has the same sample id. A slider or strip drag is
one transaction; consecutive actions on the same zone set coalesce into one. The history is
cleared when a new import is accepted or a patch / host state is loaded.

### Saved state ("unsaved" marker)

`EditStateTracker` (`Source/ZoneEdit/EditHistory.h`, no JUCE): every action records the state id
it starts from and allocates a fresh id for the state it produces; perform/redo move the tracker
to `after`, undo back to `before` (coalesced actions keep the first `before` and the last
`after`). Save / load / host-state restore store the current id as the saved one. The patch is
dirty when `current != saved` or a non-undoable change happened since the save (import
accepted, sample relinked). So undoing back to the saved state clears "unsaved", redoing past
it sets it again, and a different edit after an undo is never mistaken for the saved state.

## Live playback (real-time safety)

`replaceZone` (message thread) copies the map, edits the zone, swaps it into the processor's
copy under the existing map mutex (O(1) swap) and calls `VoiceEngine::updateMapLive`. The audio
thread picks it up at the start of the next block (`applyPendingMapUpdate`: atomic flag +
try-lock, never blocks; the old map is parked and released later on the message thread, so the
audio thread neither allocates nor frees). Sounding voices remember the index of the zone they
were started from and take over root key, fine tune, transpose, gain and pan (pitch ratio
recomputed, glide respected). Gain does not step: the voice's `GainRamp` (linear, per sample,
`kRampSeconds` = 20 ms, 882 samples at 44.1 kHz) glides from the current level to the new one,
retargeting smoothly if another edit arrives mid-ramp; a new note starts directly at its zone
gain. Key/velocity/RR edits only change which zone the next note picks; a note that is already
sounding is never cut off by a range edit. Round-robin counters are kept (zone count is
unchanged by edits).

## Audition

`VoiceEngine::requestAudition(zoneIndex, note, velocity)` / `stopAudition()` (message thread,
lock-free): the request is packed into one `std::atomic<uint64_t>` (sequence, on/off, zone,
note, velocity). At the start of each block the audio thread releases the previous audition
voice and, for an "on" request, starts a voice on exactly `zones[zoneIndex]` via
`auditionZoneNow`, bypassing `selectZone` (no key/velocity matching, no overlap resolution, no
round-robin advance). Audition voices use internal channel 17, outside MIDI 1-16, so host notes
and note-offs never collide with them. Offline samples stay silent.

## Persistence

Zone fields were already part of `.looper.json`; the only addition is the optional per-zone
`"auto"` object. Schema stays 1: older builds ignore it, older patches load without it
(Reset to auto disabled for those zones). Edits mark the patch unsaved until saved or undone.

## Tests

`Tests/ZoneEditorTests.cpp` (in CMake, the Windows workflow list and the macOS fallback list):
field edits and clamping, no inverted ranges, sanitize, overlap rules, Reset to auto,
Use detected pitch, edit records and stale-index guard, JSON + file round trip (incl. legacy
patch), live voice updates with a counting `operator new/delete` (zero allocations / frees on
the audio-thread hand-off), RR position kept across a live edit, audition note choice, and, when
JUCE is available (`LOOPER_BUILD_PLUGIN=ON`), the real `juce::UndoManager` path: undo/redo,
drag coalescing, redo stack cleared by a new edit, Reset/Use detected undoable, stale history.

`Tests/ZoneStripTests.cpp` (same three lists): strip geometry and snapping (every key centre
snaps to itself, off-strip clamps), hit-testing (shared edges between neighbours, selection
priority, root marker vs body, narrow zones, stacked layers, clipped edges), drag maths (edge
push-along and restore, clamping, root, body with group clamp, multi-zone deltas, Alt-draw),
tooltips in C4 = 60 / C3 = 60, selection rules (toggle, Shift range with anchor, Select All,
prune), mixed values, relative / absolute group edits, typed-text classification, group edit
records (skip unchanged, all-or-nothing, duplicates), saved-state tracker, gain ramp (unit and on
a sounding DC note: max sample-to-sample change <= 1.5 x jump / ramp length, half way at 10 ms,
settled at 20 ms, zero allocations), exact-zone audition (overlaps, RR alternates without
advancing the cycle, request replacement, channel isolation, invalid / offline zones, zero
allocations), and with JUCE: one undo step per multi-zone edit and per strip drag, saved state
across undo / redo / branching.

## Limitations

* Velocity ranges are edited in the fields only; the strip shows keys, not a velocity x key grid.
* Moving a zone body also moves its root (keeps the sample's pitch relative to the zone); use
  the root dot or Root field to change it back.
* Alt/Option-draw replaces the key range but leaves the root where it was (it may end up outside
  the new range; Audition then plays the nearest in-range key).
* The strip shows the range that fits every root (at least C2-C7); edges outside it are not
  grabbable until the view includes them (no zoom / scroll yet).
* A drag that ends exactly where it started still leaves an (empty) undo step and the
  "unsaved" marker.
* Hosts may swallow Cmd/Ctrl+Z / Cmd/Ctrl+A before the plugin window sees them; use the
  Undo/Redo buttons (Select All: Shift-click the first and last rows).
