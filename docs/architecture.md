# Architecture

3DSMSC is split into portable C++ modules and a Nintendo 3DS platform layer.

## Runtime flow

```text
Touch screen and top-screen cassette view
                    |
              Application state
                    |
        Library, search, and queue modules
                    |
              Library controller
                    |
              Playback controller

                    |
                 Audio engine
             /       |       \
          MP3       AAC       FLAC
             \       |       /
                 Decoder adapters
                    |
       PCM conversion and bounded buffers
                    |
                   NDSP
```

The UI never calls a decoder directly. The audio engine owns decoder state, PCM buffers, seek behavior, and transitions. The library module owns explicit scans and track metadata. The queue module owns temporary playback order.

## Platform constraints

- The 3DS ARM11 has limited memory and cooperative threading.
- Decoding must use bounded buffers and must not block the UI thread.
- Background playback requires explicit APT and NDSP lifecycle handling.
- Bundled images are converted to a 3DS-friendly format before packaging.
- User artwork is decoded with strict size and dimension limits.
- C++17 is enabled, but exceptions and RTTI remain disabled for the 3DS target.

## Library scan

The user selects a root or uses the configured default root. A scan is started only by an explicit user action. The scan state is visible and cancellable. The scanner receives a filesystem interface, so the same recursion and filtering rules are tested on the host and use the 3DS SD-card adapter at runtime. A successful scan with no playable tracks shows:

```text
Go to Settings > Library > Full scan again
```

`force_full_scan` is a user-requested action rather than an automatic background policy. The scan covers the music folders the user ticked in the folder picker (Settings > Library > Music folders, or tap the library card on the home screen), saved in `sdmc:/3dsmsc/folders.txt`. With none ticked it covers the configured default root. Overlapping folders are scanned once per track, and a folder that no longer exists is reported without discarding the others.

## Configuration

Compiled defaults are always available. `sdmc:/3dsmsc/config.toml` overrides the defaults using a deliberately small TOML subset:

- sections: `library`, `player`, and `controls`;
- string, boolean, and numeric scalar values;
- invalid values restore safe defaults and report an error.

This keeps configuration editable without adding a large parser to the 3DS binary.

## Planned modules

- `audio`: playback state, decoder adapters, PCM conversion, and NDSP integration.
- `library`: explicit filesystem scanning, metadata, artwork lookup, and indexes.
- `queue`: temporary playback order and transitions.
- `ui`: touch-screen navigation and the top-screen cassette view.
- `platform`: filesystem, input, APT, memory, and timing adapters.
- `config`: safe defaults and user configuration.
