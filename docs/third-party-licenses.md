# Third-party licenses

Every third-party decoder, image library, font, or audio component must have its source, version, and license recorded before integration.

The project must not ship code with a license incompatible with the project distribution. Preserve required copyright notices and include the complete license text in the distribution.

## minimp3

- Source: `third_party/minimp3/minimp3.h`
- License: CC0 1.0 Universal
- License file: `third_party/minimp3/LICENSE`
- Integration: MP3 frame decoding with bounded input and PCM output buffers

## dr_flac

- Source: `third_party/dr_libs/dr_flac.h`
- License: Unlicense or MIT-0, at the project's choice
- License file: `third_party/dr_libs/LICENSE`
- Integration: FLAC streaming through custom file callbacks and PCM16 output

## minimp4

- Source: `third_party/minimp4/minimp4.h`
- License: CC0 1.0 Universal
- License file: `third_party/minimp4/LICENSE`
- Integration: M4A/MP4 audio-track indexing and access-unit extraction

## FAAD2

- Source: `third_party/faad2/`
- License: GPL-2.0-or-later
- License file: `third_party/faad2/COPYING`
- Integration: raw ADTS/AAC and M4A/MP4 AAC decoding

The selected FAAD2 path makes the distributed project GPL-compatible. The complete FAAD2 source and license text must remain in the distribution.
