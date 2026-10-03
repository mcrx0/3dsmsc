#!/usr/bin/env bash
# Usage: tools/host_harness/cover_smoke.sh <binary> [cover-file]
# Starts the app with a queue and checks that the background decoder delivers a cover: a "K" draw
# command (the cover thumbnail) must appear when the album folder holds a picture file, and when
# the track has the picture embedded instead, and not at all when the setting is off. Not part of the golden comparison, because the frame on which the cover
# arrives depends on thread timing.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=$(realpath "$1")
COVER=$(realpath "${2:-$ROOT/tests/fixtures/cover.png}")
# An MP3 whose ID3v2.3 tag holds the cover as an APIC frame, followed by the silent fixture.
embed_cover() { # cover file, audio fixture, output
  python3 - "$1" "$2" "$3" <<'PY'
import struct, sys
image = open(sys.argv[1], 'rb').read()
audio = open(sys.argv[2], 'rb').read()
body = b'\x00image/png\x00\x03\x00' + image
frame = b'APIC' + struct.pack('>I', len(body)) + b'\x00\x00' + body
size = len(frame)
syncsafe = bytes([(size >> 21) & 127, (size >> 14) & 127, (size >> 7) & 127, size & 127])
open(sys.argv[3], 'wb').write(b'ID3\x03\x00\x00' + syncsafe + frame + audio)
PY
}
run() { # name, extra config, how the cover is stored: file | embedded
  local dir; dir=$(mktemp -d)
  mkdir -p "$dir/sdmc:/3dsmsc/music/Album"
  if [ "$3" = embedded ]; then
    embed_cover "$COVER" "$ROOT/tests/fixtures/silence.mp3" "$dir/sdmc:/3dsmsc/music/Album/t1.mp3"
  else
    cp "$ROOT/tests/fixtures/silence.mp3" "$dir/sdmc:/3dsmsc/music/Album/t1.mp3"
    cp "$COVER" "$dir/sdmc:/3dsmsc/music/Album/cover${COVER##*/cover}"
  fi
  printf '3dsmsc-queue-v1\n0\nsdmc:/3dsmsc/music/Album/t1.mp3\n' > "$dir/sdmc:/3dsmsc/queue.txt"
  printf '[player]\n%s\n' "$2" > "$dir/sdmc:/3dsmsc/config.toml"
  ( cd "$dir" && NOAUDIO=1 FRAMES=150 CMDS="$dir/cmds" "$BIN" >/dev/null 2>"$dir/err" ) || { cat "$dir/err"; exit 1; }
  local count; count=$(grep -c '^K ' "$dir/cmds" || true)
  echo "$1: $count cover draws" >&2
  rm -rf "$dir"
  echo "$count"
}
on=$(run "picture file" 'show_cover = true' file | tail -1)
embedded=$(run "embedded picture" 'show_cover = true' embedded | tail -1)
off=$(run "cover off" 'show_cover = false' file | tail -1)
[ "$on" -gt 0 ] && [ "$embedded" -gt 0 ] && [ "$off" -eq 0 ] && echo "cover smoke test passed" ||
  { echo "FAILED (file=$on embedded=$embedded off=$off)" >&2; exit 1; }
