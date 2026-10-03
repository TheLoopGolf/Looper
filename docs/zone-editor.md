# Manual zone editor

Fixes for anything AutoMapper got wrong, after an import is accepted, without leaving the
main screen.

## Where it lives

Main view, lower card (visible once a user instrument is loaded):

```
+-- keyboard strip ------------------------------------------------------------+
|  zones as tinted spans, root key = dot, selected zone = sand outline          |
+------------------------------------------------------------------------------+
+-- ZONES (list) ------------------+-- ZONE  <sample> [. edited] -- Undo Redo --+
| SAMPLE   ROOT  KEYS   VEL   RR   |  ROOT KEY     FINE TUNE     GAIN           |
| . Piano  C4 !  A3-E4  1-127  -   |  LOW KEY      HIGH KEY      RR GROUP       |
|   Pluck  E3    ...               |  LOW VEL      HIGH VEL      RR ALT         |
|                                  |  Detected E3 (+15 ct) . 100% confidence    |
|                                  |  ! Overlaps 1 zone: Pluck.wav . first ...  |
|                                  |  [Use detected pitch] [Reset to auto] [Audition]
+----------------------------------+--------------------------------------------+
```

* Select: click a list row, or a key inside a zone on the strip. Repeated clicks on one key
  cycle through zones stacked on it.
* Audition: click a zone's root key (dot) on the strip, or hold **Audition**. The note is the
  root clamped into the zone's key range at velocity 100 clamped into its velocity layer, so it
  plays that zone (unless another zone matches first, or RR picks an alternate).
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

Each change is a `ZoneEdit { index, sampleId, before, after }`. The plugin wraps it in
`ZoneEditAction` (`juce::UndoableAction`) and performs it on the processor's
`juce::UndoManager`. perform/undo call `LooperAudioProcessor::replaceZone`, which refuses the
action if the zone at that index no longer has the same sample id. A slider drag begins one
transaction; consecutive actions on the same zone coalesce into one. The history is cleared
when a new import is accepted or a patch / host state is loaded.

## Live playback (real-time safety)

`replaceZone` (message thread) copies the map, edits the zone, swaps it into the processor's
copy under the existing map mutex (O(1) swap) and calls `VoiceEngine::updateMapLive`. The audio
thread picks it up at the start of the next block (`applyPendingMapUpdate`: atomic flag +
try-lock, never blocks; the old map is parked and released later on the message thread, so the
audio thread neither allocates nor frees). Sounding voices remember the index of the zone they
were started from and take over root key, fine tune, transpose, gain and pan (pitch ratio
recomputed, glide respected). Key/velocity/RR edits only change which zone the next note
picks; a note that is already sounding is never cut off by a range edit. Round-robin counters
are kept (zone count is unchanged by edits).

## Persistence

Zone fields were already part of `.looper.json`; the only addition is the optional per-zone
`"auto"` object. Schema stays 1: older builds ignore it, older patches load without it
(Reset to auto disabled for those zones). Every edit marks the patch unsaved.

## Tests

`Tests/ZoneEditorTests.cpp` (in CMake, the Windows workflow list and the macOS fallback list):
field edits and clamping, no inverted ranges, sanitize, overlap rules, Reset to auto,
Use detected pitch, edit records and stale-index guard, JSON + file round trip (incl. legacy
patch), live voice updates with a counting `operator new/delete` (zero allocations / frees on
the audio-thread hand-off), RR position kept across a live edit, audition note choice, and, when
JUCE is available (`LOOPER_BUILD_PLUGIN=ON`), the real `juce::UndoManager` path: undo/redo,
drag coalescing, redo stack cleared by a new edit, Reset/Use detected undoable, stale history.

## Limitations

* One zone at a time; no drag-editing of zone edges on the strip yet.
* Gain changes on a sounding voice jump to the new level at the next audio block (no ramp).
* The patch stays marked unsaved after undoing back to the saved state.
* Audition plays through normal zone selection, so overlapping zones or RR alternates can
  sound instead of the exact selected zone.
* Hosts may swallow Cmd/Ctrl+Z before the plugin window sees it; use the Undo/Redo buttons.
