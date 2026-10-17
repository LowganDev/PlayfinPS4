#!/usr/bin/env bash
# Fails when the PS4 executable imports a module by path: the console loader only resolves bare module names
# (a host path such as /opt/pacbrew/... comes from linking a stub library by its full path).
# Usage: check-ps4-imports.sh <elf> <eboot.bin>
set -euo pipefail
elf=$1 eboot=$2
bad=$(readelf -d "$elf" | grep NEEDED | grep '/' || true)
# a library path left in eboot.bin (whole output read: grep -q would stop strings early, and pipefail would hide the
# match); other /opt/ strings, such as mpv's default configuration folder, are not imports
bad+=$(strings "$eboot" | grep -E '^/opt/.*\.(so|prx|sprx)$' || true)
if [ -n "$bad" ]; then
    echo "PS4 imports must be bare module names:" >&2
    echo "$bad" >&2
    exit 1
fi
echo "PS4 imports OK: $(readelf -d "$elf" | grep -c NEEDED) modules, all by name"
