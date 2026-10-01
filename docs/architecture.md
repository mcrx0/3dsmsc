# Architecture

3DSMSC is split into portable C++ modules and a Nintendo 3DS platform layer. Everything that does
not need libctru lives in `include/3dsmsc/` and `src/{audio,config,library,playback,queue,ui,util}` and is
covered by host tests; `src/3ds/` holds the code that only runs on the console.

## Runtime flow

```text
 Touch input and buttons ──► Application (src/3ds/app*.cpp, UI thread)
                                  │            │
              Library controller ─┘            └─► Cassette renderer (citro2d, both screens)
              Browse index, search, queue
                                  │
                                  ▼  track to play, seek requests, equalizer settings
                       Audio player (src/3ds/ndsp_player.cpp)
                                  │
 ┌────────────────────────────────┴─────────────────────────────────┐
 │ Audio thread:  decoder ──► equalizer ──► 4 NDSP wave buffers     │
 │                MP3 / AAC / FLAC   10 peaking filters             │
 └──────────────────────────────────────────────────────────────────┘
                                  │
                                 NDSP (DSP)
```

The UI thread never calls a decoder. It tells the audio player what to load, play, pause, or seek,
and reads back a snapshot of the position and state each frame. The audio player owns the decoder,
the PCM buffers, and the equalizer. The library module owns scans and metadata, and the queue
module owns play order.

## Modules

- `audio`: the decoder interface and the MP3, AAC (M4A/MP4 and raw ADTS), and FLAC adapters, plus
  the equalizer. All are portable and bounded: input is read through fixed windows, and a decoder
  never allocates per frame.
- `library`: the filesystem interface, the recursive scanner, metadata readers (filename fallback,
  ID3, FLAC, MP4), sidecar artwork lookup, the track index and search, the Artist → Album → Track
  browse index, the folder picker, the saved folder list, and the library cache.
- `playback`: the controller that connects the queue to an `AudioPlayer`: which file plays, what
  happens when a track ends, skipping unplayable files, repeat, and seeking. It sees the audio
  engine only through the `AudioPlayer` interface, so a fake drives it in the tests.
- `queue`: the queue, its play order (sequential or shuffled), and queue file persistence.
- `config`: compiled defaults and the small TOML subset loader and writer.
- `ui`: the view state shared with the renderer, the screen layout constants used by both drawing
  and touch input, the drill-down browse navigator, the equalizer editing rules, and the text of the
  About and Button mapping screens.
- `util`: small shared helpers, such as the RAII file handle.
- `src/3ds/`: `main.cpp` (start-up and shutdown of the platform), the `App` class (input, screens,
  and the frame loop, split over `app*.cpp`), the citro2d renderer, and the NDSP player.

The dependency rule: portable modules never include libctru, and `src/3ds/` calls into them, never
the other way. See `docs/coding-standards.md`.

## Threads

| Thread | Stack | Does |
| --- | --- | --- |
| UI (main) | 32 KB | Input, scans, drawing at the display rate, saving files |
| Audio | 64 KB | Decoding, equalizing, filling NDSP buffers, carrying out seeks |

The audio thread runs at a higher priority than the UI thread so a slow frame cannot starve it. The
UI and audio threads share only small values (state, position, a pending seek, the equalizer gains),
which are written as 32-bit values and published with a revision counter so neither can read a
half-written update.

## Platform constraints

- The 3DS ARM11 is a single 268 MHz core on an Old 3DS, with slow SD card access. Work that is
  invisible on a PC, such as listing a folder once per file, freezes the console.
- **The main thread has only a 32 KB stack** (libctru's default `__stacksize__`). Large buffers and
  objects must live on the heap; the audio player, with its 16 KB decode buffer, is heap-allocated
  for this reason. `scripts/check-stack-usage.sh` measures every frame and fails the build when one
  is too large (see `docs/building.md`).
- Audio needs the DSP firmware dump at `sdmc:/3ds/dspfirm.cdc`. Its absence is reported on the
  status line, and the rest of the UI keeps working.
- Decoding uses bounded buffers and never runs on the UI thread, including seeking.
- Background playback needs explicit APT and NDSP lifecycle handling.
- C++17 is enabled, but exceptions and RTTI remain disabled for the 3DS target.
- Text is parsed once and cached; text drawing dominates the per-frame cost otherwise.

## Library scan

The user chooses folders or uses the configured default root. A scan starts only after an explicit
user action, its state is visible, and it can be cancelled. The scanner receives a filesystem
interface, so the same recursion and filtering rules are tested on the host and use the 3DS
SD-card adapter at runtime. A successful scan with no playable tracks shows:

```text
Go to Settings > Full scan again
```

Reading tags is the slow part. The readers skip cover art by seeking past it (an embedded cover
can be megabytes), the artwork lookup lists each folder once, and directory entry types come from
the directory read instead of a `stat` per file.

After a successful scan the result is written to `sdmc:/3dsmsc/library.cache` (versioned binary,
written to a temporary file and renamed) and loaded again at start-up. A missing, truncated, or
corrupt cache is ignored, and the claimed track count is checked against the file size so a bad
file cannot request a huge allocation.

The scan covers the music folders the user ticked in the folder picker (Settings > Music folders,
or tap the library card on the home screen), saved in `sdmc:/3dsmsc/folders.txt`. With none ticked
it covers the configured default root. Overlapping folders are scanned once per track, and a folder
that no longer exists is reported without discarding the others. A cancelled or failed scan keeps
the previous library.

## Audio pipeline

1. The decoder returns one block of interleaved PCM (1152 frames for MP3, 1024 for AAC, up to 4096
   for FLAC).
2. Blocks are gathered until a buffer holds about 2048 frames, then expanded to stereo if needed.
3. The equalizer filters the block in place. It is skipped entirely when every band is flat.
4. Four such buffers are queued to NDSP strictly in order, so SD card or system stalls of a couple
   of hundred milliseconds do not interrupt the sound.

Pause uses the DSP's own pause, so resuming continues exactly where it stopped. A seek is a request
recorded by the UI thread and carried out by the audio thread: it clears the queued audio, moves
the decoder, and refills. Repeated requests add up, because each starts from the position the
previous one asked for. MP3 seeking skips frames without decoding them until the last 300 ms.

At the end of a track the worker reports `Stopped`; a decode failure is reported as `Error`, so the
app can tell a finished track from a broken one and stop after three failures in a row.

## Configuration

Compiled defaults are always available. `sdmc:/3dsmsc/config.toml` overrides the defaults using a
deliberately small TOML subset: sections, string, boolean, and numeric scalar values, and `#`
comments on their own line or after a value. A file
with an invalid line or value is rejected as a whole: the app starts with every default and
reports the error. Unknown keys are ignored.

| Section | Keys |
| --- | --- |
| `library` | `music_root`, `exclude_hidden`, plus `last_selected_root`, `scan_scope`, `force_full_scan` (kept for older configs) |
| `player` | `volume`, `animation_enabled`, `animation_speed`, `search_case_sensitive`, `background_playback`, `theme`, `battery_display`, `repeat`, `shuffle`, `seek_seconds` |
| `equalizer` | `enabled`, `bands` (ten comma-separated dB values) |
| `controls` | Button names. Read and saved, but button mapping is fixed in code for now |

## Diagnostics

`sdmc:/3dsmsc/boot.log` records each start-up step, the audio engine's result code, the library
cache load time, and any frame slower than 100 ms with a split between logic and drawing. It is
the first thing to read when something freezes.
