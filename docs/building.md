# Building

## Host tools

The host build requires:

- CMake 3.20 or newer;
- Ninja or Make;
- a C++17 compiler.

The 3DS build also needs Python 3 plus `librsvg2` and `libcairo2` (for example
`sudo apt install librsvg2-2 libcairo2`): `scripts/build-icons.py` rasterizes `assets/icons/*.svg` into
the icon sheet that `tex3ds` packs into the `.3dsx`. Without Python or `tex3ds` the app still builds,
with text labels instead of icons.

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 3DS tools

On the current x86_64 Arch-based host, run:

```sh
./scripts/artifacts/setup-3ds-host.sh
source scripts/artifacts/3ds-env.sh
```

The setup script uses `DEVKITPRO` and `DEVKITARM` and requires sudo to install the official devkitPro packages. To verify an existing installation without changing the host, run `./scripts/artifacts/setup-3ds-host.sh --check`.

Install devkitPro packages for devkitARM, libctru, and the 3DS packaging tools. The Linux build expects these environment variables:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
```

Configure the 3DS build with the repository toolchain file:

```sh
cmake -S . -B build/3ds -G Ninja \
  -DBUILD_3DS=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
cmake --build build/3ds
```

The `3dsmsc-3ds` target produces the ELF executable. When `3dsxtool` and `smdhtool` are available, CMake also produces `build/3ds/3dsmsc.3dsx` with embedded SMDH metadata.

The `.3dsx` can be copied to a folder on the SD card, for example:

```text
/3ds/3dsmsc/3dsmsc.3dsx
```

The later CIA target will be added after the `.3dsx` path is validated on hardware. The SMDH metadata is generated from the `APP_NAME`, `APP_VERSION`, `APP_MAINTAINER`, and `APP_DESCRIPTION` values in `CMakeLists.txt`; the current maintainer is `@mcrx0` and the current version is `0.5.2`.

## Continuous integration

`.github/workflows/build.yml` runs the host test suite and a `clang-format` check of every project source file, then builds the `.3dsx` with devkitPro, runs the stack-usage check, and verifies the package. The workflow uploads the package as the `3dsmsc-3dsx` artifact.

## Release validation

Before distributing a build:

1. Run the host tests and 3DS build locally.
2. Copy the generated `.3dsx` to an SD card and test on both New and Old Nintendo 3DS hardware. The Old 3DS (one 268 MHz core, less memory) is the worst case for stalls and stack or memory limits.
3. Walk through the smoke tests in `docs/testing.md`: scanning, saved library, MP3/AAC/M4A/MP4/FLAC playback, seeking, repeat and shuffle, the equalizer, search, browse, and lid/sleep behavior.
4. Test corrupted, unsupported, hidden, and large library files.
5. Confirm `docs/third-party-licenses.md` and the FAAD2 GPL distribution requirements are included.

## Assets

`assets/main_ui.jpg` is a visual reference for the cassette screen. `assets/icon.png` is the 48x48
launcher icon, generated from `assets/icon.svg` by `scripts/build-icons.py` in its app-icon mode (run
it by hand after editing the SVG, and commit the PNG). The UI icons are editable SVGs in `assets/icons/`: change a file, rebuild, and the icon sheet is
regenerated. Icons are drawn white and tinted at runtime, and `assets/icons/order.txt` fixes their
order, which must match `IconId` in `src/3ds/icons.hpp`. Cover art found in a library is recorded
but not drawn yet.

## 3DS build in a container (no devkitPro install)

If `apt.devkitpro.org` is unreachable or you prefer not to install devkitPro, build with the
official image. The script installs `librsvg2-2`/`libcairo2` inside the container for icon
generation, and the finished package is copied to `dist/3dsmsc.3dsx`.

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkitarm sh -c '
  apt-get update -qq && apt-get install -y -qq librsvg2-2 libcairo2
  export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
  export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
  cmake -S . -B build/3ds -G Ninja -DBUILD_3DS=ON -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
  cmake --build build/3ds'
```

See [container-workflow.md](container-workflow.md) for the whole container flow, including how to
replay CI locally.

## Quality checks

```sh
scripts/check-quality.sh     # clang-format, host tests, clang-tidy (needs clang-format and clang-tidy)
```

CI runs the same script. The rules are in [coding-standards.md](coding-standards.md).

## Stack usage check

The 3DS main thread has only a 32 KB stack (libctru's default `__stacksize__`), so a large local
buffer or object in `main()` or in anything it calls overflows on the console while a PC, with an
8 MB stack, never notices. Run this after changing code that runs on the main thread:

```sh
scripts/check-stack-usage.sh        # fails if any single frame exceeds 8 KB
# or, without a local devkitPro:
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkitarm bash scripts/check-stack-usage.sh
```

Large objects belong on the heap (the audio player, which holds a 16 KB decode buffer, is
heap-allocated for this reason). The audio thread has its own 64 KB stack.
