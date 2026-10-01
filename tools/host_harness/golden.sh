#!/usr/bin/env bash
# Usage: tools/host_harness/golden.sh <binary> <outdir> [seeds...]
# Runs scripted + fuzz sessions deterministically (no audio, fixed clock, no sleeping) and writes
# a SHA-256 of everything drawn per session, so two builds can be compared byte for byte.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
H=$ROOT/build/harness
[ $# -ge 2 ] || { echo "usage: $0 <binary> <outdir> [seeds...]" >&2; exit 2; }
# Sessions run in temporary folders, so both paths must be absolute.
BIN=$(realpath "$1"); mkdir -p "$2"; OUT=$(realpath "$2"); shift 2
SEEDS=${@:-1 2 3 4 5 6 7 8}
[ -x "$BIN" ] || { echo "not an executable: $BIN" >&2; exit 1; }
: > "$OUT/summary.txt"
run_session() { # name, state(fresh|cache|light), keys-file-or-string, frames
  local name=$1 state=$2 keys=$3 frames=$4 dir; dir=$(mktemp -d)
  mkdir -p "$dir/sdmc:/3dsmsc/music/Album"
  for i in 1 2 3; do cp $ROOT/tests/fixtures/silence.mp3 "$dir/sdmc:/3dsmsc/music/Album/t$i.mp3"; done
  case $state in
    cache) $H/make_cache "$dir/sdmc:/3dsmsc/library.cache" 40 ;;
    light) $H/make_cache "$dir/sdmc:/3dsmsc/library.cache" 40
           printf '[player]\ntheme = "light"\nrepeat = "one"\nshuffle = true\nseek_seconds = 25\n[equalizer]\nenabled = true\nbands = "3,2,1,0,-1,0,1,2,3,4"\n' > "$dir/sdmc:/3dsmsc/config.toml" ;;
  esac
  ( cd "$dir" && DETERMINISTIC=1 NOAUDIO=1 FRAME_MS=0 CMDS="$OUT/$name.cmds" FRAMES=$frames KEYS="$keys" timeout 300 "$BIN" >/dev/null 2>"$OUT/$name.err"; echo $? > "$OUT/$name.exit" )
  rm -rf "$dir"
  printf "%-26s exit=%s lines=%-8s sha=%s\n" "$name" "$(cat $OUT/$name.exit)" "$(grep -vc '^F ' $OUT/$name.cmds)" "$(grep -v '^F ' $OUT/$name.cmds | sha256sum | cut -c1-20)" >> "$OUT/summary.txt"
}
for seed in $SEEDS; do
  for state in fresh cache light; do run_session "fuzz${seed}_$state" $state "$(cat $H/fuzz$seed.keys)" 700 & done
  wait
done
# scripted flows (each opens a different screen); keys in the harness syntax
run_session "flow_about_controls" cache "5:TOUCH@250,210,10:UP,14:A,20:DOWN,24:DOWN,28:DOWN,32:B,36:UP,40:A,44:DOWN,48:DOWN,52:B,56:B" 70
run_session "flow_equalizer" light "5:TOUCH@250,210,10:DOWN,14:DOWN,18:DOWN,22:DOWN,26:A,30:TOUCH@100,100,32:TOUCH@100,150,40:X,44:Y,48:DDOWN,50:A,54:B,58:B" 70
run_session "flow_queue_modes" cache "5:TOUCH@80,210,10:TOUCH@80,95,14:TOUCH@250,95,18:DOWN,22:DOWN,26:A,30:B" 40
run_session "flow_folders_browse" cache "5:TOUCH@240,160,9:DOWN,12:A,16:DOWN,20:X,24:B,28:B,30:TOUCH@80,40" 40
sort -o "$OUT/summary.txt" "$OUT/summary.txt"; cat "$OUT/summary.txt" | awk '{print $1, $2, $3, $4}' | column -t | head -40
