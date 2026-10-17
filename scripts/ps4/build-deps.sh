#!/bin/bash
# Build the PS4 dependencies (pacbrew packages) from the recipes of scripts/ps4, in the image scripts/build-ps4.sh
# uses. The packages land in build-deps/, where build-ps4.sh takes them first, ready to be published as the
# "deps" release of the repository.
# Usage: scripts/ps4/build-deps.sh [package...]   (default: all, in dependency order)
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="xfangfang/pacbrew:250221"
PACKAGES="${*:-musl dav1d mbedtls SDL2 freetype harfbuzz libass ffmpeg libmpv libcurl}"

mkdir -p "$ROOT/build-deps"

# The image entrypoint runs `cd /src; bash -c "$@"`: pass a single command string.
docker run --rm -v "$ROOT":/src "$IMAGE" "
set -e
apt-get update -qq
apt-get install -y -qq --no-install-recommends sudo xz-utils lzma fakeroot file \
  autoconf automake libtool nasm makepkg meson >/dev/null
useradd -m -s /bin/bash builder
echo 'builder ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/builder
cp -r /src/scripts/ps4 /home/builder/ps4
chown -R builder:builder /home/builder/ps4
# packages built earlier in build-deps/ satisfy the dependencies of the ones built now
for f in /src/build-deps/*.pkg.tar.gz; do
  [ -e \"\$f\" ] && pacman --noconfirm -U \"\$f\" >/dev/null
done
for pkg in $PACKAGES; do
  echo \"=== building \$pkg\"
  su - builder -c \"cd ps4/\$pkg && makepkg -siCf --noconfirm\"
  cp /home/builder/ps4/\$pkg/*.pkg.tar.gz /src/build-deps/
done
chown -R $(id -u):$(id -g) /src/build-deps
ls -la /src/build-deps
"
