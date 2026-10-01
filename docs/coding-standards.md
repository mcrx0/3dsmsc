# Coding standards

These rules are enforced by tools, not by memory: `scripts/check-quality.sh` runs them, and CI
fails on any violation. Where a rule needs a reason, the reason is here.

## Layout of the code

- **Portable first.** Anything that does not need libctru goes in `include/3dsmsc/` and
  `src/{audio,config,library,playback,queue,ui,util}`, where host tests can reach it. `src/3ds/`
  holds only what needs the console: input, drawing, NDSP, and the `App` that wires them together.
- **Rules live in the portable layer.** What happens when a track ends, how a touch maps to an
  equalizer band, which rows a settings screen has: these are decisions, so they are portable and
  tested. `App` reads the console's input, calls them, and fills the `CassetteView`.
- **Hardware behind an interface.** The audio engine is reached through `AudioPlayer`
  (`include/3dsmsc/audio/player.hpp`). NDSP implements it on the console; the tests use a fake.
- **One concern per file.** A file that needs a table of contents should be split (see how
  `src/library/tag_*.cpp` split the metadata readers, and `src/3ds/app_*.cpp` the application).
- **Screen coordinates in one place.** `include/3dsmsc/ui/layout.hpp` is used by both the renderer
  and the input code, so a button is where it looks like it is.

## Naming (checked by `readability-identifier-naming`)

| Thing | Style | Example |
| --- | --- | --- |
| Types, enums | `PascalCase` | `PlaybackController`, `RepeatMode` |
| Enum values (`enum class`) | `PascalCase` | `Panel::Equalizer` |
| Functions, methods | `snake_case` | `load_current` |
| Variables, parameters | `snake_case` | `start_ms` |
| Constants | `snake_case` | `max_block_bytes` |
| Private data members | `snake_case_` | `loaded_path_` |
| Namespaces | `lower_case` | `threedsmsc::layout` |

No `k` prefixes and no Hungarian notation. Use `enum class`, never a plain `enum`.

## Language rules

- C++17, no exceptions, no RTTI (the 3DS build disables both). Report failure with a return value
  and an error string, as the existing code does.
- Files are owned by `UniqueFile` (`include/3dsmsc/util/unique_file.hpp`), never a bare `FILE*`.
- Every struct field and every view field has a default initializer. The renderer reads all of
  them every frame; an uninitialized `eq_visible` once drew the equalizer over the home screen.
- A number that means something has a name (`healthy_playback_ms`, not `2000`). Parsers of binary
  formats may use the format's own offsets, with a comment naming the field.
- Comments say why, not what. A reader can see the loop; they cannot see that the SD card is slow.
- A function that needs more than a cognitive complexity of 25 is doing too much: split it.
- No allocation per frame in the UI loop or per block in a decoder.
- The main thread's stack is 32 KB. Large objects go on the heap; `scripts/check-stack-usage.sh`
  fails when any frame exceeds 8 KB.

## Tests

- Every portable rule has a host test, and a test is only trusted once it has failed against the
  bug it guards: revert the fix, watch it fail, restore it.
- A new decision goes into a portable module and its test, not into `src/3ds/`.
- Test files are split by module (`tests/unit/*_tests.cpp`) and share `test_support.hpp`.
- The console-only code is covered by the host harness (`tools/host_harness/README.md`), which runs
  the real `src/3ds` code against fake libctru headers and compares every draw command.

## Tools

| Tool | Config | What it enforces |
| --- | --- | --- |
| clang-format | `.clang-format` | Layout: Google-based, 100 columns |
| clang-tidy | `.clang-tidy` | Bugs, performance, naming, complexity (warnings are errors) |
| `check-stack-usage.sh` | none | Main-thread stack frames under 8 KB |
| `-Wall -Wextra` | `CMakeLists.txt` | Compiler warnings, none tolerated |

The tidy checks are a curated list. A check stays only if it finds real problems here; checks that
merely restated the style were removed, and `.clang-tidy` says why each disabled one is off.

`third_party/` is vendored as received and is excluded from every rule.
