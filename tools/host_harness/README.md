# Host harness

Runs the real console application code (`src/3ds/*.cpp`) on a PC. libctru and citro2d are replaced
by fake headers (`stubs/`) and two stub sources: `runtime_stub.cpp` (input, time, threads, NDSP) and
`render_stub.cpp`, which records every draw command instead of drawing.

It exists because the touch and button logic cannot run in the host unit tests, and an emulator is
slow and not scriptable. With it, a refactor of the application can be proved to change nothing:
the sequence of draw commands for a scripted session must be byte-identical before and after.

## Build

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON && cmake --build build   # the portable library
tools/host_harness/build.sh [zero|poison|plain]
```

All variants build with AddressSanitizer and UBSan. `zero` initialises locals to 0 (deterministic),
`poison` fills them with a pattern so a read of an uninitialised value changes the output.

## Run

The binary is driven by environment variables:

| Variable | Meaning |
| --- | --- |
| `FRAMES` | Frames to run before exiting |
| `KEYS` | Script, `frame:EVENT,...`: for example `5:TOUCH@250,210,10:UP,15:A` |
| `DETERMINISTIC=1` | A fixed clock, so timing-dependent code behaves the same every run |
| `NOAUDIO=1` | Behave as if the DSP firmware were missing |
| `HEADPHONES=1` | Report headphones plugged in |
| `FRAME_MS` | Simulated milliseconds per frame (`0` for none) |
| `CMDS=file` | Write every draw command to a file |

`render_preview.py` and `preview_driver.cpp` render a set of screen states to image files for a
visual check of the renderer.

## Cover smoke test

```sh
tools/host_harness/cover_smoke.sh build/harness/hostmain_zero [cover-file]
```

Starts the app with a queue and checks that the background decoder delivers the cover, from a
picture file in the album folder and from a picture embedded in the MP3 (a `K` draw command
appears), and that nothing is drawn with `show_cover = false`. It is
separate from the golden comparison because the frame on which the cover arrives depends on thread
timing.

## Golden comparison

```sh
tools/host_harness/golden.sh $PWD/build/harness/hostmain_zero $PWD/build/harness/golden_new 1 2 3
```

Runs a set of sessions (random key scripts from `make_fuzz_keys.py`, with a fresh, cached, and
light-theme start, plus scripted flows through every screen) and writes a SHA-256 of each session's
draw commands to `summary.txt`. Run it on the old code, keep that `summary.txt`, then run it on
the new code and `diff` the two files. Absolute paths are required.
