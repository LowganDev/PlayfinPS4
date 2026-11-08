#!/usr/bin/env bash
# The README's screenshots (docs/images/screens/), made by the Linux build against fake_jellyfin's showcase: the
# Blender Foundation's open films with their free artwork (scripts/showcase/CREDITS.md), the French UI.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --build --preset linux-debug
build=build/linux-debug
art=$build/showcase-art
out=$build/showcase-out
python3 scripts/showcase/fetch-art.py "$art"
rm -rf "$out"
mkdir -p "$out" docs/images/screens
"$build/tests/fake_jellyfin" --https --port 48920 --url-file "$out/url" --showcase "$art" > /dev/null &
server=$!
trap 'kill $server 2>/dev/null || true; wait $server 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do
    [ -s "$out/url" ] && break
    sleep 0.1
done
LC_ALL=fr_FR.UTF-8 SDL_VIDEODRIVER=offscreen MESA_LOADER_DRIVER_OVERRIDE=llvmpipe timeout 300 \
    "$build/playfin" --hidden --no-discovery --screen shell --data-dir "$out/data" \
    --test-script scripts/showcase/showcase.script --test-output "$out" \
    --fixed-time 2026-10-16T19:45:00Z --utc-offset 120
# JPEG for the README (a fifth of the PNG's size)
python3 - "$out" docs/images/screens <<'PY'
import glob, os, sys
from PIL import Image
for path in glob.glob(os.path.join(sys.argv[1], "*.png")):
    name = os.path.splitext(os.path.basename(path))[0] + ".jpg"
    Image.open(path).convert("RGB").save(os.path.join(sys.argv[2], name), quality=85, optimize=True)
PY
ls -l docs/images/screens/
