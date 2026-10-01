# Testing

## Host tests

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The host tests (`tests/unit/*_tests.cpp`, one file per module, sharing `test_support.hpp`) cover
the portable modules:

- **Settings:** defaults, parsing and fallback, the config file round trip, the seek step, repeat
  mode, theme, and the equalizer band list (including rejection of bad lists).
- **Library:** supported format detection, case-sensitive and case-insensitive search, recursive
  scanning, hidden-file filtering, cancellation, empty-library handling, several scan folders with
  overlap and missing folders, and the folder picker (including an unreadable folder).
- **Tags:** filename fallback, ID3 (Latin-1, UTF-16, UTF-8, track and disc numbers), a large embedded
  cover being skipped, FLAC, MP4/M4A, and MP3 length from a Xing header, the bitrate, or no tag.
- **Browsing:** the Artist → Album → Track index, its ordering, and the drill-down navigator.
- **Artwork:** sidecar priority, and one folder listing shared by every track of an album.
- **Queue:** navigation, persistence, and shuffle (each pass visits every track once, never starts
  with the track that just played, supports previous and select, and turns off cleanly).
- **Library cache:** a round trip with accents and symbols, an atomic replace, and rejection of
  truncated, corrupt, wrong-version, and oversized files.
- **Audio:** every decoder played to the end, MP3 length and long-file decoding (more than the
  decoder's input window), MP3 seeking, and seeking support per format.
- **Playback rules:** skipping unplayable tracks, the failure limit, repeat and end-of-queue, seeking,
  and status messages, against a fake audio player.
- **Equalizer editor and layout:** touch-to-band and touch-to-gain mapping, every editing action, and
  that the screen layout constants do not overlap.
- **Equalizer:** measured gain on test tones (boost, cut, distant bands untouched), skipped bands at
  low sample rates, saturation instead of wrap-around, and presets.

The audio thread, the touch UI, and the renderer need the console or an emulator and are covered by
the smoke tests below. Local filesystem traversal is tested against real temporary folders.

## Host harness

The touch and button logic in `src/3ds/` is exercised by `tools/host_harness/`, which runs the real
application code against fake libctru headers and records every draw command. A scripted or fuzzed
session produces a hash; a refactor must leave every hash unchanged. See
`tools/host_harness/README.md`.

## Quality checks

`scripts/check-quality.sh` runs clang-format, the host tests, and clang-tidy, the same as CI. The
rules behind it are in `docs/coding-standards.md`.

## Stack usage

The 3DS main thread has a 32 KB stack, which a PC test cannot exercise. After changing code that
runs on the main thread, run `scripts/check-stack-usage.sh` (see `docs/building.md`). It fails when
any function needs more than 8 KB of stack.

## 3DS smoke tests

After building `.3dsx`, with the DSP firmware in place (`sdmc:/3ds/dspfirm.cdc`):

1. Copy the file to the SD card and launch it from the Homebrew Launcher.
2. Verify both screens draw and the bottom screen shows the home view.
3. Choose folders in **Settings > Music folders**, then run **Settings > Full scan again**. Verify
   the progress counter, that B cancels, and that Browse and Search then work.
4. Close and reopen the app: the library, the queue, and the theme must still be there, with no
   rescan.
5. Play MP3, AAC/M4A/MP4, and FLAC tracks. Verify the total time, the countdown, and the progress bar.
6. Verify play, pause, previous, next, volume, and the seek buttons at each seek step.
7. Verify repeat (off, all, one) and shuffle, from both Settings and the Queue screen.
8. Open the equalizer: drag a bar, use the d-pad, apply presets, and confirm the sound changes.
9. Close the lid and confirm playback continues.
10. Plug and unplug headphones and confirm the header icon follows. Check the battery in the header
    (icon, percentage, or off under **Settings > UI > Battery**), and that it turns accent coloured
    while charging and red below 15%.
11. Verify `Settings > Full scan again` after a scan of an empty folder.
12. Open **Settings > Misc > Button mapping** and **About this**: scroll to the end of each, and check that B returns to Settings and that the listed buttons match what they do.
13. Read `sdmc:/3dsmsc/boot.log`: it should list every start-up step and no slow frames during idle.

Citra reports no headphones and needs the DSP firmware in its emulated SD card. Steps 2 to 8 and 11
can be checked there; steps 9 and 10 (the lid and the headphones) need a console.

## Fixtures

The host fixtures can be regenerated with:

```sh
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -codec:a libmp3lame -b:a 64k -write_xing 0 tests/fixtures/silence.mp3
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -codec:a flac tests/fixtures/silence.flac
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -c:a aac tests/fixtures/silence.aac
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -c:a aac tests/fixtures/silence.m4a
```

## Hardware test set

The hardware test set should include:

- one MP3 with ID3 metadata and one with a large embedded cover;
- one MP3 with no tags, and one variable-bitrate MP3;
- one raw AAC or M4A file;
- one audio-only MP4 file;
- one FLAC file with embedded metadata;
- one file with no metadata;
- one corrupted file;
- one unsupported file;
- one folder with `cover.png` and one with `cover.jpg`;
- a library of several thousand tracks, to check scan time, start-up time, and search speed.

Measure memory usage and frame stability while playing each format, with the equalizer on and off.
