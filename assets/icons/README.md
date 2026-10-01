# UI icons

Mock icon set for the touch UI. Each `*.svg` is a 24x24 white glyph on a transparent
background; the app tints them at runtime, so keep them single-colour and white.

Edit any SVG, then run `scripts/build-icons.py`. It rasterizes every icon to 32x32 PNG
(uses `librsvg` + `libcairo` through ctypes, no pip packages) and writes `icons.t3s`,
which `tex3ds` packs into `romfs/gfx/icons.t3x` during the CMake build.

The icon order in `icons.t3s` is the index the renderer uses (`IconId` in
`src/3ds/icons.hpp`). Adding an icon means adding a name to both.
