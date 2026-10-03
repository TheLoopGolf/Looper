# Disk streaming

Long samples no longer have to be fully decoded into RAM. Looper keeps the **first N frames**
(the *preload*, default 64k frames = 1.5 s at 44.1 kHz) of each long sample in memory and
streams the rest from disk while a voice plays. Short samples (no longer than the preload) stay
fully in RAM exactly as before.

## What you see

- **Memory chip** (main view header, left of the gear): `RAM 24 MB · Streaming`, `RAM 81 MB · In RAM`
  or, in sand, `RAM 24 MB · 3 dropouts`. The golf ball is drawn "in flight" (speed lines) while
  streaming, and a small pip lights while a voice is actually reading from disk. Hover for
  details (samples streamed, decoded size, stream buffers, preload); click to open
  **Settings → Memory**.
- **Settings → Memory**
  - **Sample memory**: *Stream long samples* (default) or *Load fully into RAM*. Saved **per
    patch** (`map.loadIntoRam` in `.looper.json`) and therefore in the host session. Use *Load
    fully* for small patches or if your disk is slow.
  - **Preload size**: 16k ... 1024k frames (session pref `preloadFrames`, default 65536). Bigger =
    more RAM, more tolerance for slow disks and extreme pitch-up.
  - **Status**: live line, e.g. `RAM 24 MB · Streaming · 8 of 9 samples streamed · 2 voices
    streaming now · 0 dropouts`, plus **Reset dropout count**.

Changing either setting re-decodes the current patch's samples (message thread, short pause).

## Design

```
Import / patch load (message thread)
  ImportController::decodeFile
    short sample or loadIntoRam -> full SampleBuffer (as before)
    else                         -> SampleBuffer{ preload frames, residentFrames, stream = FileStreamSource }
  SamplePool  (immutable snapshots + hazard pointers; replaced buffers retired, freed by a 1 s GC timer)

Audio thread (VoiceEngine::processBlock, 256-frame sub-blocks)
  note-on        -> lock-free buffer lookup, play from preload at once, DiskStreamer::acquire(slot)
  render         -> Hermite read: index < residentFrames ? preload : stream ring window
  release/steal  -> DiskStreamer::release(slot) (CAS; safe even while a reader is mid-fill)

Reader threads (DiskStreamer, 2 threads)
  pick the most urgent Active slot (buffered frames / playback rate), CAS Active->Busy,
  FileStreamSource::readFrames (per-source mutex, LRU cap of 48 open readers), publish writeEnd
```

- **Lock- and allocation-free audio thread.** Slots and rings (160 slots x 16384 stereo frames,
  about 20 MB) are allocated once, the first time any streamed sample is loaded; fully-in-RAM
  patches pay nothing. The audio thread only does atomics/CAS and a shared_ptr copy. The
  StreamingTests suite counts allocations on the audio thread and requires zero.
- **Seamless boundary.** Frame `f` of a sample comes from the preload when `f < residentFrames`
  and from the ring otherwise, so the 4-point Hermite window simply straddles both. Output is
  **bit-identical** to fully-loaded playback (tested at ratios 0.37...4.0, 48k/96k files, with
  glide and sample-start offsets, and for 16/24-bit, mono/stereo/4-channel WAV).
- **Pitch-aware read-ahead.** Each voice publishes its read floor and playback rate; readers
  serve the slot with the least *time* buffered (frames / rate), so a voice two octaves up gets
  serviced four times sooner. The ring never overwrites frames at or after the read floor.
- **Underruns.** If the ring runs dry the voice holds its last sample and fades to silence over
  64 samples, counts one dropout, publishes where it now is so the reader skips ahead, and fades
  back in when data arrives. Read errors are treated the same way (counted separately). No
  glitch, no crash, no blocking.
- **Slots.** Voice release, steal, retrigger and engine reset free their slot at once. If all
  160 are busy the new voice plays its preload and retries; exhaustion is counted.
- **Offline bounce.** When the host renders non-realtime (`isNonRealtime()`), voices read
  missing frames **synchronously** (`fillBlocking`), so a bounce never has dropouts.
- **Live swap.** Relocate, zone edits and sample replacement publish a new pool snapshot;
  playing voices keep their old buffer (and slot) alive until they end; the retired buffer is
  freed later on the message thread.

## Benchmark (Linux box, 96 x 20 s stereo 24-bit 44.1 kHz WAVs = 485 MB on disk)

`cmake -DLOOPER_BUILD_BENCH=ON ...`, then `./StreamingBench [dir] [numFiles] [seconds]`
(page cache dropped with `posix_fadvise` before each load; best effort).

| Mode | Load time | Sample RAM | + stream rings | Process RSS delta |
|---|---|---|---|---|
| Load fully into RAM | 0.95-1.08 s | 646 MB | - | 660 MB |
| Streaming (64k preload) | 0.10 s | 48 MB | 20 MB | 36 MB |

| Playback (64 voices, ratios 1/2/4) | Audio | Audio-thread cost per 256-frame block | Dropouts |
|---|---|---|---|
| Fully in RAM | 10 s | 470-522 us | - |
| Streaming, offline bounce (blocking reads) | 10 s | 568-591 us | 0 |
| Streaming, real-time paced (reader threads) | 6 s | 688-694 us | 0 |

About 10x faster loading and about 13x less sample memory, for roughly 15-50 % more
audio-thread time with 64 simultaneous streaming voices (the extra cost is the ring indexing
and stream bookkeeping). 75 M frames streamed, 0 read errors.

## Limitations

- Pitch detection on import of a streamed sample analyses its first 8 s (enough for any
  one-shot; very long drones with late pitch changes may differ).
- Reader threads poll every 1 ms when idle (Windows timer granularity can make that ~15 ms; the
  default preload covers 370 ms even at +2 octaves).
- Ring memory (~20 MB) stays allocated once anything has streamed, until the plugin unloads.
- Slot count is fixed (160); beyond that voices play their preload only until a slot frees.
- The streamer reads to the end of the file even if the zone's sample end is earlier.
- Bit-exactness is tested on WAV; AIFF/FLAC rely on JUCE's reader seeking, which is exact for
  those formats but is not covered by the bit-exact tests. Compressed formats (MP3/OGG) seek
  approximately.
- Switching mode or preload size re-decodes all samples synchronously on the message thread.
- Not yet tested inside a real DAW or on slow/network drives.
