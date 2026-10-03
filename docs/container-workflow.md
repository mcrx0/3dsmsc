# Working in the devkitPro container

Every 3DS build, static-analysis run, and CI replay for this project was done inside the official
`devkitpro/devkitarm` image, so nothing had to be installed on the host except a container engine
(podman here; docker works the same). This page records what runs where and why, so the flow can
be repeated without guessing.

## Why a container

- **No toolchain install.** devkitPro's package server was unreachable from the development
  machine, and the image already holds devkitARM, libctru, citro2d, citro3d, `3dsxtool`, `tex3ds`,
  and `smdhtool`.
- **The same versions as CI.** The GitHub workflow runs in this image. The image is Debian, with GCC 12
  and clang 14. A newer host compiler accepts code GCC 12 rejects (a test using `Rig({})` compiled
  locally and failed in CI until it was found this way), and a newer clang-format lays code out
  differently.
- **A clean environment.** Nothing on the host leaks into the build.

## What runs where

| Task | Where | Command |
| --- | --- | --- |
| Host unit tests | Host | `cmake -S . -B build -G Ninja -DBUILD_TESTING=ON && cmake --build build && ctest --test-dir build` |
| Host harness (the console code on a PC, with sanitizers) | Host | `tools/host_harness/build.sh`, see its README |
| 3DS build (`dist/3dsmsc.3dsx`) | Container | below |
| Stack-usage check | Container (needs the ARM compiler) | below |
| clang-format, clang-tidy | Container (or any host with clang 14) | `scripts/check-quality.sh` |
| The whole CI pipeline | Container | `scripts/ci-local.sh` |

The harness stays on the host because it needs AddressSanitizer and runs native code, not ARM.

## Build the `.3dsx`

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkitarm sh -c '
  apt-get update -qq && apt-get install -y -qq librsvg2-2 libcairo2
  export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
  export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
  cmake -S . -B build/3ds -G Ninja -DBUILD_3DS=ON -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
  cmake --build build/3ds'
```

The package is written to `build/3ds/3dsmsc.3dsx` and copied to `dist/3dsmsc.3dsx`. The copy is
`copy_if_different`, so an unchanged build leaves the file's timestamp alone.

Notes:

- `librsvg2-2` and `libcairo2` are installed on every run because the container is thrown away
  afterwards. They are only needed to render the SVG icons into the icon sheet.
- The image already has `cmake` and `ninja`. For the checks below, `clang-format`, `clang-tidy`, and
  `g++` are added the same way.
- The bind mount (`-v "$PWD":/src`) makes the container write `build/` and `dist/` into the working
  tree. With rootless podman those files belong to your user. With docker they belong to root. The
  `apt-get` steps need root, so end the container command with `chown -R "$UIDGID" /src` and pass
  `-e UIDGID="$(id -u):$(id -g)"`, or clean up with `sudo`.

## Stack check and quality checks

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkitarm bash scripts/check-stack-usage.sh
```

`scripts/check-quality.sh` runs clang-format, the host tests, and clang-tidy. In the container, install
the tools first:

```sh
podman run --rm -v "$PWD":/src -w /src docker.io/devkitpro/devkitarm bash -c '
  apt-get update -qq && apt-get install -y -qq clang-format clang-tidy cmake ninja-build g++ git
  git config --global --add safe.directory /src
  bash scripts/check-quality.sh'
```

`safe.directory` is needed because the files belong to a different user inside the container, which
makes git refuse to read the repository.

clang-tidy is slow (a few minutes) because it parses every translation unit with the standard
library. It runs on the portable code with the real build flags, and on `src/3ds/` against the fake
libctru headers in `tools/host_harness/stubs/`, since the real headers need the ARM toolchain.

## Replay CI before pushing

```sh
scripts/ci-local.sh          # or: scripts/ci-local.sh docker
```

It clones the last commit into a temporary directory and runs, in the container, the same steps as
`.github/workflows/build.yml`: the host job (`check-quality.sh`), then the 3DS job (build, stack
check, package check). Commit first: uncommitted changes are not in a clone. A pass here is a good
prediction of a pass on GitHub; the one thing it cannot show is GitHub-specific behaviour such as
the artifact upload.

## The flow, end to end

1. Edit and run the host tests and, for console code, the harness (fast, on the host).
2. Run `scripts/check-quality.sh` (in the container, for matching tool versions).
3. Build the `.3dsx` in the container and test it on Citra or a console.
4. Commit, then `scripts/ci-local.sh` to confirm CI will pass.
5. Push to `monitor`: the workflow runs the same two jobs and uploads the `.3dsx` as an artifact.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `fatal: detected dubious ownership` | Add `git config --global --add safe.directory <path>` inside the container |
| Tests compile on the host but not in the container | The container's GCC 12 is stricter; fix the code, not the container |
| clang-format wants to change files you did not touch | Your host's clang-format differs from the container's; format inside the container |
| Linker errors for `swkbd*` | `<3ds.h>` must be included before `<3ds/applets/swkbd.h>` |
| The icon sheet is missing from the package | `librsvg2-2`/`libcairo2` were not installed in the container |
| Image pull fails | Pull `docker.io/devkitpro/devkitarm` once while online; it is cached afterwards |
