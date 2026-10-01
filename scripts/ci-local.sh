#!/usr/bin/env bash
# Replays the GitHub workflow (.github/workflows/build.yml) on this machine, in the same container
# image, against a clean clone of the last commit. Use it to find out whether a push would pass CI.
# Uncommitted changes are not included: commit first.
#
# Usage: scripts/ci-local.sh [podman|docker]     (default: podman)
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engine="${1:-podman}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git clone -q "$root" "$work/repo"
cat > "$work/ci.sh" <<'INNER'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends clang-format clang-tidy cmake ninja-build g++ git \
  librsvg2-2 libcairo2 >/dev/null
git config --global --add safe.directory /w
cd /w
echo "== host job: format, tests, static analysis"
bash scripts/check-quality.sh
echo "== package-3ds job: build, stack, package"
cmake -S . -B build/3ds -G Ninja -DBUILD_3DS=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake >/dev/null
cmake --build build/3ds
bash scripts/check-stack-usage.sh
test "$(head -c 4 build/3ds/3dsmsc.3dsx)" = "3DSX"
echo "CI replay passed"
INNER
"$engine" run --rm -v "$work/repo":/w -v "$work/ci.sh":/ci.sh:ro docker.io/devkitpro/devkitarm bash /ci.sh
