#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
  printf 'Usage: %s [--check]\n' "$0"
}

fail() {
  printf 'error: %s\n' "$1" >&2
  return 1
}

check_host() {
  if [[ "$(uname -s)" != Linux ]]; then
    fail 'This setup script requires Linux.'
    return 1
  fi
  case "$(uname -m)" in
    x86_64|aarch64) ;;
    *)
      fail "Unsupported host architecture: $(uname -m). Use x86_64 or aarch64."
      return 1
      ;;
  esac

  if ! command -v cmake >/dev/null 2>&1; then
    fail 'cmake is required.'
    return 1
  fi
  if ! command -v ninja >/dev/null 2>&1; then
    fail 'ninja is required.'
    return 1
  fi
  if ! command -v g++ >/dev/null 2>&1; then
    fail 'A host C++ compiler is required.'
    return 1
  fi

  local devkitpro="${DEVKITPRO:-/opt/devkitpro}"
  local devkitarm="${DEVKITARM:-$devkitpro/devkitARM}"
  local tool_path="$devkitpro/tools/bin:$devkitarm/bin"
  PATH="$tool_path:$PATH"
  export DEVKITPRO="$devkitpro" DEVKITARM="$devkitarm" PATH

  if [[ ! -x "$DEVKITARM/bin/arm-none-eabi-g++" ]]; then
    fail "devkitARM compiler not found under DEVKITARM=$DEVKITARM."
    return 1
  fi
  if [[ ! -f "$DEVKITPRO/libctru/include/3ds.h" ]]; then
    fail "libctru headers not found under DEVKITPRO=$DEVKITPRO."
    return 1
  fi
  if [[ ! -f "$DEVKITPRO/libctru/lib/libctru.a" && ! -f "$DEVKITPRO/libctru/armv6k/lib/libctru.a" ]]; then
    fail "libctru library not found under DEVKITPRO=$DEVKITPRO."
    return 1
  fi
  if ! command -v 3dsxtool >/dev/null 2>&1; then
    fail '3dsxtool was not found in DEVKITPRO/tools/bin or DEVKITARM/bin.'
    return 1
  fi
  if [[ ! -f "$DEVKITARM/arm-none-eabi/lib/3dsx.specs" ]]; then
    fail "3dsx.specs not found under DEVKITARM=$DEVKITARM."
    return 1
  fi

  printf 'Host is ready: DEVKITPRO=%s DEVKITARM=%s\n' "$DEVKITPRO" "$DEVKITARM"
  printf '3DSX tool: %s\n' "$(command -v 3dsxtool)"
}

ensure_devkitpro_master_key() {
  local key_id='BC26F752D25B92CE272E0F44F7FD5492264BB9D0'
  if ! pacman-key --list-keys 2>/dev/null | grep -q "$key_id"; then
    sudo pacman-key --recv "$key_id" --keyserver keyserver.ubuntu.com
  fi
  sudo pacman-key --lsign "$key_id"
}

install_devkitpro() {
  if command -v dkp-pacman >/dev/null 2>&1; then
    command -v sudo >/dev/null 2>&1 || fail 'sudo is required to install devkitPro packages.'
    sudo dkp-pacman -S --needed --noconfirm 3ds-dev
    return
  fi

  command -v pacman >/dev/null 2>&1 || fail 'Install devkitPro pacman or run this script on an Arch-based host.'
  command -v sudo >/dev/null 2>&1 || fail 'sudo is required to install devkitPro packages.'

  ensure_devkitpro_master_key
  sudo pacman -U --needed --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst
  if ! grep -q '^\[dkp-libs\]' /etc/pacman.conf || ! grep -q '^\[dkp-linux\]' /etc/pacman.conf; then
    printf '\n[dkp-libs]\nServer = https://pkg.devkitpro.org/packages\n\n[dkp-linux]\nServer = https://pkg.devkitpro.org/packages/linux/$arch/\n' | \
      sudo tee -a /etc/pacman.conf >/dev/null
  fi
  sudo pacman -Sy --noconfirm
  sudo pacman -S --needed --noconfirm 3ds-dev
}

if [[ "${1:-}" == --check ]]; then
  [[ $# -eq 1 ]] || fail 'Only --check is supported.'
  check_host
  exit 0
fi
[[ $# -eq 0 ]] || { usage >&2; exit 2; }

if ! check_host; then
  install_devkitpro
fi
check_host
printf 'Source %s before building in this shell.\n' "$script_dir/3ds-env.sh"
