#!/usr/bin/env bash
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
export DEVKITPRO DEVKITARM
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"

if [[ ! -x "$DEVKITARM/bin/arm-none-eabi-g++" ]]; then
  printf 'error: devkitARM is not installed at DEVKITARM=%s\n' "$DEVKITARM" >&2
  return 1 2>/dev/null || exit 1
fi
if [[ ! -f "$DEVKITPRO/libctru/include/3ds.h" ]]; then
  printf 'error: libctru is not installed at DEVKITPRO=%s\n' "$DEVKITPRO" >&2
  return 1 2>/dev/null || exit 1
fi
