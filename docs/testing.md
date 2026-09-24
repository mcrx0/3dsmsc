# Testing

## Host tests

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The host tests cover:

- default settings;
- configuration parsing and fallback behavior;
- supported audio path detection;
- case-sensitive and case-insensitive search;
- queue navigation;
- recursive library scanning;
- hidden-file filtering;
- scanner cancellation and empty-library handling;
- filename metadata fallback;
- ID3, FLAC, and MP4/M4A metadata reading;
- artwork priority and sidecar lookup;
- nested folder browsing;
- queue file persistence;
- library controller default and selected-root scans;
- bounded MP3 decoding using `tests/fixtures/silence.mp3`;
- FLAC streaming decoding using `tests/fixtures/silence.flac`;
- raw AAC and M4A decoding using AAC fixtures;
- local filesystem traversal.

## 3DS smoke tests

After building `.3dsx`:

1. Copy the file to the SD card.
2. Launch it from the Homebrew Launcher.
3. Verify the top and bottom screens initialize.
4. Verify a configured library path can be selected and scanned.
5. Verify MP3, AAC/M4A/MP4, and FLAC tracks.
6. Verify play, pause, seek, next, previous, volume, and queue transitions.
7. Close the lid and confirm playback continues.
8. Verify `Settings > Library > Full scan again` after an empty scan.

The MP3 host fixture can be regenerated with:

```sh
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -codec:a libmp3lame -b:a 64k -write_xing 0 tests/fixtures/silence.mp3
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -codec:a flac tests/fixtures/silence.flac
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -c:a aac tests/fixtures/silence.aac
ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=stereo -t 2 -c:a aac tests/fixtures/silence.m4a
```


The hardware test set should include:

- one MP3 with ID3 metadata;
- one raw AAC or M4A file;
- one audio-only MP4 file;
- one FLAC file with embedded metadata;
- one file with no metadata;
- one corrupted file;
- one unsupported file;
- one folder with `cover.png`;
- one folder with `cover.jpg`.

Measure memory usage and frame stability while playing each format.
