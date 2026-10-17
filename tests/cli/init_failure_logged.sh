#!/usr/bin/env bash
# A window/GL failure at startup must end up in playfin.log (on the PS4 it is the only readable trace).
# SDL's "dummy" video driver has no OpenGL, so creating the window fails. Usage: init_failure_logged.sh <playfin> <dir>
set -uo pipefail
playfin=$1 dir=$2
rm -rf "$dir"
SDL_VIDEODRIVER=dummy timeout 30 "$playfin" --hidden --data-dir "$dir" 2>/dev/null
status=$?
if [ $status -ne 1 ]; then
    echo "expected exit code 1, got $status" >&2
    exit 1
fi
grep -q "SDL_CreateWindow failed" "$dir/playfin.log" || { echo "failure not in $dir/playfin.log" >&2; exit 1; }
echo "startup failure logged"
