#!/usr/bin/env bash
# Reports the stack frame of every function and fails when one is too big for the 3DS main
# thread, whose stack is only 32 KB (libctru's default __stacksize__). A big local buffer or
# object in main() or in anything it calls overflows silently on the console while working
# fine on a PC, where the stack is 8 MB.
#
# Usage: scripts/check-stack-usage.sh [limit-bytes]    (default 8192)
# Needs DEVKITARM/DEVKITPRO (or the devkitpro/devkitarm container, see docs/building.md).
set -euo pipefail
limit="${1:-8192}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
devkitpro="${DEVKITPRO:-/opt/devkitpro}"
devkitarm="${DEVKITARM:-$devkitpro/devkitARM}"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
flags="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -DARM11 -O3 -DNDEBUG -std=c++17 \
  -fno-exceptions -fno-rtti -fstack-usage -I $root/include -I $root/third_party/minimp3 \
  -I $root/third_party/minimp4 -I $root/third_party/dr_libs -I $root/third_party/faad2/include \
  -I $devkitpro/libctru/include -I $devkitpro/portlibs/3ds/include -I $root/src/3ds"
cd "$out"
for source in "$root"/src/*/*.cpp; do
  case "$source" in *_impl.cpp) continue ;; esac   # third-party decoders run on the audio thread
  "$devkitarm/bin/arm-none-eabi-g++" $flags -c "$source" -o "$(basename "$source" .cpp).o" 2>/dev/null
done
echo "Largest stack frames (bytes):"
cat ./*.su | awk -F'\t' '{print $2"\t"$1}' | sort -rn | awk 'NR <= 8'
worst=$(cat ./*.su | awk -F'\t' '{print $2}' | sort -rn | awk 'NR == 1')
if [ "${worst:-0}" -gt "$limit" ]; then
  echo "FAIL: a frame of $worst bytes exceeds the $limit byte limit" >&2
  exit 1
fi
echo "OK: every frame is within $limit bytes"
