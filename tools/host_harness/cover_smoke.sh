#!/usr/bin/env bash
# Usage: tools/host_harness/cover_smoke.sh <binary> [cover-file]
# Starts the app with a queue whose album folder holds a cover, and checks that the background
# decoder delivers it: a "K" draw command (the cover thumbnail) must appear, and again not at all
# when the setting is off. Not part of the golden comparison, because the frame on which the cover
# arrives depends on thread timing.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=$(realpath "$1")
COVER=$(realpath "${2:-$ROOT/tests/fixtures/cover.png}")
run() { # name, extra config, expected K count is zero?
  local dir; dir=$(mktemp -d)
  mkdir -p "$dir/sdmc:/3dsmsc/music/Album"
  cp "$ROOT/tests/fixtures/silence.mp3" "$dir/sdmc:/3dsmsc/music/Album/t1.mp3"
  cp "$COVER" "$dir/sdmc:/3dsmsc/music/Album/cover${COVER##*/cover}"
  printf '3dsmsc-queue-v1\n0\nsdmc:/3dsmsc/music/Album/t1.mp3\n' > "$dir/sdmc:/3dsmsc/queue.txt"
  printf '[player]\n%s\n' "$2" > "$dir/sdmc:/3dsmsc/config.toml"
  ( cd "$dir" && NOAUDIO=1 FRAMES=150 CMDS="$dir/cmds" "$BIN" >/dev/null 2>"$dir/err" ) || { cat "$dir/err"; exit 1; }
  local count; count=$(grep -c '^K ' "$dir/cmds" || true)
  echo "$1: $count cover draws" >&2
  rm -rf "$dir"
  echo "$count"
}
on=$(run "cover on" 'show_cover = true' | tail -1)
off=$(run "cover off" 'show_cover = false' | tail -1)
[ "$on" -gt 0 ] && [ "$off" -eq 0 ] && echo "cover smoke test passed" || { echo "FAILED (on=$on off=$off)" >&2; exit 1; }
