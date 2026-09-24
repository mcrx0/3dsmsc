# Building

## Host tools

The host build requires:

- CMake 3.20 or newer;
- Ninja or Make;
- a C++17 compiler.

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

The later CIA target will be added after the `.3dsx` path is validated on hardware. The SMDH metadata is generated from the `APP_NAME`, `APP_VERSION`, `APP_MAINTAINER`, and `APP_DESCRIPTION` values in `CMakeLists.txt`; the current maintainer is `@mcrx0` and the current version is `0.4.2b`.

## Continuous integration

`.github/workflows/build.yml` runs the host test suite and formatting checks, then builds and verifies the `.3dsx` package with devkitPro. The workflow uploads the package as the `3dsmsc-3dsx` artifact.

## Release validation

Before distributing a build:

1. Run the host tests and 3DS build locally.
2. Copy the generated `.3dsx` to an SD card and test on New Nintendo 3DS hardware.
3. Verify MP3, AAC/M4A/MP4, and FLAC playback, seeking, queue navigation, search, albums, artwork, and lid/sleep behavior.
4. Test corrupted, unsupported, hidden, and large library files.
5. Confirm `docs/third-party-licenses.md` and the FAAD2 GPL distribution requirements are included.

## Assets

`assets/main_ui.jpg` is a reference image. Runtime artwork is loaded from detected sidecar files through the citro3d texture importer; keep artwork dimensions and file sizes bounded for 3DS memory limits.
