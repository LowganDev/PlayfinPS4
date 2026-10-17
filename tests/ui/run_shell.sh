#!/usr/bin/env bash
# End to end: fake Jellyfin over HTTPS (self-signed) on 127.0.0.1:48920, the shell trusts it, signs in, browses.
# Usage: run_shell.sh <playfin> <playfin_imgdiff> <fake_jellyfin> <script> <references-dir> <output-dir>
#            ["fake_jellyfin options"] ["playfin options"]
set -euo pipefail
playfin=$1 imgdiff=$2 fake=$3 script=$4 refs=$5 out=$6
fakeOptions=${7:-}
appOptions=${8:-}
rm -rf "$out"
mkdir -p "$out"
# shellcheck disable=SC2086
"$fake" --https --port 48920 --url-file "$out/url" $fakeOptions > /dev/null &
server=$!
trap 'kill $server 2>/dev/null || true; wait $server 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do
    [ -s "$out/url" ] && break
    sleep 0.1
done
LC_ALL=fr_FR.UTF-8 SDL_VIDEODRIVER=offscreen MESA_LOADER_DRIVER_OVERRIDE=llvmpipe timeout 120 \
    "$playfin" --hidden --no-discovery --screen shell --data-dir "$out/data" --test-script "$script" --test-output "$out" \
    $appOptions
if grep -q "tok-demo" "$out/data/playfin.log"; then
    echo "the access token reached the log" >&2
    exit 1
fi
shopt -s nullglob
references=("$refs"/*.png)
if [ ${#references[@]} -eq 0 ]; then
    echo "no reference images in $refs (scripts/update-ui-refs.sh)" >&2
    exit 1
fi
status=0
for ref in "${references[@]}"; do
    "$imgdiff" "$ref" "$out/$(basename "$ref")" || status=1
done
exit $status
