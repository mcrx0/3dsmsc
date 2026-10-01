#!/usr/bin/env bash
# The checks CI runs on the code, in one command: formatting, static analysis, and the host
# tests. Run it before pushing (docs/coding-standards.md explains the rules behind it).
#
# Usage: scripts/check-quality.sh
# Needs clang-format, clang-tidy, cmake, and ninja. The 3DS-only checks (the build and the stack
# limit) need devkitPro and are separate: see docs/building.md.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

sources=$(git ls-files 'src/*.cpp' 'src/*.hpp' 'include/*.hpp' 'tests/*.cpp' 'tests/*.hpp' \
  'tools/host_harness/*.cpp')

echo "== clang-format"
# shellcheck disable=SC2086
clang-format --dry-run --Werror $sources

echo "== host build and tests"
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
cmake --build build
ctest --test-dir build --output-on-failure

echo "== clang-tidy (portable code)"
tidy_sources=$(git ls-files 'src/audio/*.cpp' 'src/config/*.cpp' 'src/library/*.cpp' \
  'src/playback/*.cpp' 'src/queue/*.cpp' 'src/ui/*.cpp')
# shellcheck disable=SC2086
clang-tidy -p build --quiet $tidy_sources

echo "== clang-tidy (3DS application code, against the host harness stubs)"
for file in src/3ds/*.cpp; do
  clang-tidy --quiet "$file" -- -std=c++17 -fno-exceptions -fno-rtti -Itools/host_harness/stubs \
    -Iinclude -Ithird_party/minimp3 -Ithird_party/minimp4 -Ithird_party/dr_libs \
    -Ithird_party/faad2/include -Isrc/3ds
done
echo "all checks passed"
