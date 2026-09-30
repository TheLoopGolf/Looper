# Relocate missing samples

## Behaviour
1. **Load never fails on missing audio.** `applyPatch` keeps every zone; samples whose resolved path does not exist (or cannot be decoded) are recorded in `offlineSampleIds_` and have no buffer. `VoiceEngine::noteOn` stays **silent** for a zone whose sample has no buffer (it only falls back to the demo tone when no zone matches).
2. **Entry points.** Opening a patch with missing files jumps straight to the Relocate screen. Otherwise (e.g. host state recall) the main view shows a sand "N samples missing · Relocate…" chip in the action bar.
3. **Relocate screen** (`Source/UI/RelocateView.*`, same chrome as Review / Settings): table of Sample · Original path (tail-trimmed) · Status · New location. Actions: **Search folder…**, **Locate…** (also row double-click), **Skip**, **Close/Done**. Scans run on a one-thread `juce::ThreadPool`; results are applied on the message thread; closing / destroying the view cancels the scan.
4. **Apply.** Every match is applied immediately via `LooperAudioProcessor::relocateSample` → `ImportController::loadFileIntoPool` with the original `SampleRef` (same id, so zones keep pointing at it) → the new buffer is swapped into `SamplePool` under its mutex. The `SampleRef.path` becomes the new absolute path and `patchDirty_` is set ("· unsaved" in the status line); `PatchStore::save` rewrites paths relative to the patch file.

## Matching (`Source/PatchStore/SampleRelocator.*`, no JUCE)
- **Name match:** file name equal after case folding (ASCII + UTF-8 Latin-1 capitals À–Þ).
- **Tail score:** number of equal trailing path components, file name first (`/old/Grand/Soft/C4.wav` vs `/new/Grand/Soft/C4.wav` = 3). Original paths are split on both `/` and `\`, so Windows (`C:\…`, `\\server\share\…`) originals match on any OS.
- **Best candidate:** highest tail score; tie-break exact-case name, then shortest path, then lexicographic. A tie on tail score sets `ambiguous` and lists `alternatives`.
- **Sibling consensus (search):** an ambiguous sample picks the candidate predicted by the folder structure of samples that were found unambiguously (`viaCascade`).
- **Cascade (`predictLocations`):** given one relocation old→new, walk both parent chains in parallel; if another original lives under the old ancestor, re-apply its relative path under the new ancestor (same folder, sibling folders, renamed parents). Last prediction: same folder as the located file. `cascadeFrom` checks predictions for existence, then recursively searches the located file's folder.
- **Limits:** `RelocatorOptions` — 250 000 files, depth 32, optional cancel flag; symlinked directories are not followed.

## Tests (`Tests/RelocatorTests.cpp`)
String helpers, exact match, case-insensitive, best tail among duplicates, ambiguity flag, sibling consensus, cascade from one found file (+ sub-folder search), nothing found / bad folder / truncated index, Windows-style paths & drive letters & UNC, spaces + unicode (Latin-1 case folding, CJK), PatchStore round trip (missing → relocate → save writes new relative path → reload clean). All use temp directories.
