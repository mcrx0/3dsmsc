#!/usr/bin/env bash
# Builds the host harness: the real 3DS application code (src/3ds/*.cpp) compiled for the PC
# against fake libctru/citro2d headers, so the app can be run, scripted, and fuzzed without a console.
#
# Usage: tools/host_harness/build.sh [zero|poison|plain]
#   zero    sanitizers, uninitialised locals set to 0 (the default; deterministic)
#   poison  sanitizers, uninitialised locals set to a poison pattern (finds reads of garbage)
#   plain   sanitizers only
# Needs the host build first: cmake -S . -B build -G Ninja && cmake --build build
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
variant=${1:-zero}
case "$variant" in
  zero) init="-ftrivial-auto-var-init=zero" ;;
  poison) init="-ftrivial-auto-var-init=pattern" ;;
  plain) init="" ;;
  *) echo "usage: $0 [zero|poison|plain]" >&2; exit 2 ;;
esac
[ -f build/lib3dsmsc_core.a ] || { echo "build the host library first (see the header of this script)" >&2; exit 1; }
out=build/harness
mkdir -p "$out"
# shellcheck disable=SC2086
g++ -std=c++17 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-rtti -pthread $init \
  -Itools/host_harness/stubs -Iinclude -Ithird_party/minimp3 -Ithird_party/minimp4 \
  -Ithird_party/dr_libs -Ithird_party/faad2/include -Isrc/3ds \
  src/3ds/*.cpp tools/host_harness/render_stub.cpp tools/host_harness/runtime_stub.cpp \
  build/lib3dsmsc_core.a build/libfaad2_decoder.a -lm -o "$out/hostmain_$variant"
g++ -std=c++17 -Iinclude tools/host_harness/make_cache.cpp build/lib3dsmsc_core.a -o "$out/make_cache"
echo "built $out/hostmain_$variant"
