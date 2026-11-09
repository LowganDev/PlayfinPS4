#!/usr/bin/env bash
# Stops the test server (containers and channel encoders); the data stays in $DATA.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${SHOWCASE:-}" ]; then
    export DATA=${DATA:-$HOME/.local/share/playfin-showcase}
else
    export DATA=${DATA:-$HOME/.local/share/playfin-testserver}
fi
export UID GID
GID=$(id -g)
docker compose -f "$HERE/compose.yml" down
for pidfile in "$DATA"/run/ch*.pid; do
    [ -f "$pidfile" ] || continue
    kill "$(cat "$pidfile")" 2>/dev/null
    rm -f "$pidfile"
done
