#!/usr/bin/env bash
# Regenerates the UI reference screenshots of every tests/ui/*.script. Review the images before committing.
# shell.script and media.script run against the fake Jellyfin server (HTTPS, self-signed) on 127.0.0.1:48920;
# media.script with the test server's views and the guide's clock fixed (as ctest's ui_media).
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --build --preset linux-debug
server=
trap '[ -n "$server" ] && { kill $server 2>/dev/null || true; wait $server 2>/dev/null || true; }' EXIT
for script in tests/ui/*.script; do
    name=$(basename "$script" .script)
    out=build/linux-debug/ui-refs/$name
    rm -rf "$out"
    mkdir -p "$out" "tests/ui/references/$name"
    screen=$name
    fakeOptions=
    appOptions=
    if [ "$name" = media ]; then
        screen=shell
        fakeOptions=--media
        appOptions="--fixed-time 2026-10-16T19:45:00Z --utc-offset 120"
    fi
    if [ "$screen" = shell ]; then
        # shellcheck disable=SC2086
        build/linux-debug/tests/fake_jellyfin --https --port 48920 --url-file "$out/url" $fakeOptions > /dev/null &
        server=$!
        for _ in $(seq 1 50); do
            [ -s "$out/url" ] && break
            sleep 0.1
        done
    fi
    SDL_VIDEODRIVER=offscreen MESA_LOADER_DRIVER_OVERRIDE=llvmpipe timeout 120 \
        build/linux-debug/playfin --hidden --no-discovery --screen "$screen" --data-dir "$out/data" \
        --test-script "$script" --test-output "$out" $appOptions
    if [ -n "$server" ]; then
        kill $server 2>/dev/null || true
        wait $server 2>/dev/null || true
        server=
    fi
    rm -f "tests/ui/references/$name"/*.png
    cp "$out"/*.png "tests/ui/references/$name/"
done
ls -la tests/ui/references/*
