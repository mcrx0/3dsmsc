# 3DSMSC

3DSMSC is a lightweight Nintendo 3DS homebrew music player for offline SD-card libraries.

## App metadata

- Name: `3dsmsc`
- Version: `0.4.2b`
- Maintainer: `@mcrx0`
- Description: `A lightweight offline music player for the Nintendo 3DS`

## Current milestone

The repository currently contains the first implementation milestone:

- portable C++ configuration defaults and a safe subset TOML loader;
- track format detection for MP3, AAC, M4A, MP4, and FLAC paths;
- searchable library index data structures;
- a basic playback queue;
- an explicit recursive SD-card library scanner with cancellation and hidden-file filtering;
- filename metadata fallback, ID3/FLAC/MP4 embedded tags, and sidecar artwork lookup;
- a folder browser for selecting nested library roots;
- queue persistence at `sdmc:/3dmms/queue.txt`;
- a procedural citro2d cassette view with animated reel state and touch-screen navigation shell;
- a library controller that loads config and runs explicit default/selected scans;
- a bounded minimp3 MP3 decoder, dr_flac FLAC decoder, and FAAD2/minimp4 AAC path;
- an NDSP playback path with pause, volume, queue navigation, and background sleep control;
- a minimal 3DS entry point and `.3dsx` build target;
- host-side unit tests and build documentation.

The remaining milestone is hardware validation, broader metadata coverage, and refining the touch UI for larger libraries.

## Host build

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 3DS build

On the current x86_64 Arch-based host, install the 3DS toolchain with:

```sh
./scripts/artifacts/setup-3ds-host.sh
source scripts/artifacts/3ds-env.sh
```

The setup script requires sudo to install the official devkitPro packages. It uses `DEVKITPRO` and `DEVKITARM` for the toolchain paths. Verify an existing installation with `./scripts/artifacts/setup-3ds-host.sh --check`.

Install devkitARM, libctru, and the 3DS tools, then set `DEVKITPRO` and `DEVKITARM`:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
cmake -S . -B build/3ds -G Ninja \
  -DBUILD_3DS=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
cmake --build build/3ds
```

The generated executable is placed under `build/3ds/`. A `.3dsx` is generated when `3dsxtool` is available.

## Documentation

- `docs/architecture.md` describes the runtime modules and data flow.
- `docs/building.md` contains the build and deployment details.
- `docs/testing.md` describes host and hardware validation.
- `config/config.toml.example` documents the runtime configuration.
- `assets/main_ui.jpg` is a visual reference for the cassette screen.
