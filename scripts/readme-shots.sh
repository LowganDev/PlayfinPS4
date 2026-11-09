#!/usr/bin/env bash
# The README's screenshots (docs/images/screens/), made by the Linux build, the English UI.
#   scripts/readme-shots.sh            films from fake_jellyfin's showcase: the Blender Foundation's open films with
#                                      their free artwork (scripts/showcase/CREDITS.md)
#   scripts/readme-shots.sh DATA_DIR   films from a real server: DATA_DIR is a Playfin data folder signed in to it
#                                      (sign in once with the Linux build's --data-dir); a copy is used, the
#                                      server-*.script steps follow the library they were written for
#   scripts/readme-shots.sh --media    only music and Live TV
# Music and Live TV come from the showcase server (SHOWCASE=1 scripts/test-server/up.sh: real free albums and
# channels), when it runs.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --build --preset linux-debug
build=build/linux-debug
out=$build/showcase-out
rm -rf "$out"
mkdir -p "$out" docs/images/screens
run() {  # run DATA_DIR SCRIPT [OPTIONS...]
    local data=$1 script=$2
    shift 2
    LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 SDL_VIDEODRIVER=offscreen MESA_LOADER_DRIVER_OVERRIDE=llvmpipe timeout 400 \
        "$build/playfin" --hidden --no-discovery --screen shell --data-dir "$data" \
        --test-script "$script" --test-output "$out" "$@"
}
if [ "${1:-}" = --media ]; then
    :
elif [ $# -ge 1 ]; then
    for script in scripts/showcase/server-*.script; do
        rm -rf "$out/server-data"
        cp -a "$1" "$out/server-data"
        run "$out/server-data" "$script"
    done
    rm -rf "$out/server-data"
else
    art=$build/showcase-art
    python3 scripts/showcase/fetch-art.py "$art"
    # Sintel's video for the player's screenshot: its artwork as a still picture, as long as the film (15 min)
    [ -s "$art/sc-sintel.mp4" ] || ffmpeg -v error -y -loop 1 -framerate 1 -i "$art/sc-sintel-backdrop.jpg" -t 900 \
        -vf "scale=1920:-2,pad=1920:1080:(ow-iw)/2:(oh-ih)/2" -c:v libx264 -tune stillimage -preset veryfast \
        -pix_fmt yuv420p -g 60 "$art/sc-sintel.mp4"
    "$build/tests/fake_jellyfin" --https --port 48920 --url-file "$out/url" --showcase "$art" > /dev/null &
    server=$!
    trap 'kill $server 2>/dev/null || true; wait $server 2>/dev/null || true' EXIT
    for _ in $(seq 1 50); do
        [ -s "$out/url" ] && break
        sleep 0.1
    done
    run "$out/data" scripts/showcase/showcase.script --fixed-time 2026-10-16T19:45:00Z --utc-offset 120
fi
# music and Live TV: the showcase server, its own clock (the programmes on now)
if curl -s --max-time 5 http://127.0.0.1:8097/System/Info/Public | grep -q '"ServerName":"Playfin Showcase"'; then
    run "$out/media-data" scripts/showcase/media.script
else
    echo "the showcase server is not running (SHOWCASE=1 scripts/test-server/up.sh): music and Live TV kept" >&2
fi
# JPEG for the README (a fifth of the PNG's size)
python3 - "$out" docs/images/screens <<'PY'
import glob, os, sys
from PIL import Image
for path in glob.glob(os.path.join(sys.argv[1], "*.png")):
    name = os.path.splitext(os.path.basename(path))[0] + ".jpg"
    Image.open(path).convert("RGB").save(os.path.join(sys.argv[2], name), quality=85, optimize=True)
PY
ls -l docs/images/screens/
