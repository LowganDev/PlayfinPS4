#!/usr/bin/env bash
# Builds the PS4 pkg in the pacbrew Docker image, with the dependency packages of scripts/ps4/deps.txt (from
# build-deps/ when present, else the "deps" release). Output: dist/IV0001-PLFN00000_00-PLFN000000100000.pkg
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="xfangfang/pacbrew:250221"
BASE_URL="${PS4_DEPS_URL:-https://github.com/LowganDev/PlayfinPS4/releases/download/deps}"

# The image entrypoint runs `cd /src; bash -c "$@"`: the whole build must be one command string.
docker run --rm -v "$ROOT":/src -e OPENORBIS=/opt/pacbrew/ps4/openorbis "$IMAGE" "
set -e
for pkg in \$(grep -v '^#' scripts/ps4/deps.txt); do
  file=ps4-openorbis-\${pkg}-any.pkg.tar.gz
  if [ -f build-deps/\$file ]; then pacman --noconfirm -U build-deps/\$file; else pacman --noconfirm -U $BASE_URL/\$file; fi
done
trap 'chown -R $(id -u):$(id -g) build/ps4' EXIT
cmake --preset ps4-release
cmake --build --preset ps4-release -j\$(nproc)
"
"$ROOT/scripts/check-ps4-imports.sh" "$ROOT/build/ps4/playfin" "$ROOT/build/ps4/pkg/eboot.bin"
python3 "$ROOT/scripts/check-ps4-eboot.py" "$ROOT/build/ps4/playfin" "$ROOT/build/ps4/pkg/eboot.bin"
python3 "$ROOT/scripts/check-ps4-licenses.py" "$ROOT/build/ps4/CMakeFiles/playfin.dir/link.txt" \
    "$ROOT/resources/licenses/manifest.txt"
mkdir -p "$ROOT/dist"
cp "$ROOT/build/ps4/pkg/IV0001-PLFN00000_00-PLFN000000100000.pkg" "$ROOT/dist/"
ls -la "$ROOT/dist/IV0001-PLFN00000_00-PLFN000000100000.pkg"
