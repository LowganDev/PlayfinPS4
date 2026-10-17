#!/usr/bin/env bash
# Starts the test server: media (made once), the 3 live channel encoders (host ffmpeg, HLS into $DATA/hls), the
# Jellyfin and streams containers, then the set-up through the API. Jellyfin: http://<this PC>:8097.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
export DATA=${DATA:-$HOME/.local/share/playfin-testserver}
export UID GID
GID=$(id -g)
FONT="$HERE/../../resources/fonts/Sora-Bold.ttf"
mkdir -p "$DATA/config" "$DATA/cache" "$DATA/recordings" "$DATA/hls" "$DATA/run"
"$HERE/make-media.sh"

# one live channel: a test pattern with its name and the clock, a tone; 720p H.264 + AAC in 2 s HLS segments
channel() {  # channel <n> <source> <name> <tone Hz>
    local n=$1 dir="$DATA/hls/ch$1" pidfile="$DATA/run/ch$1.pid"
    if [ -f "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
        return
    fi
    rm -rf "$dir"
    mkdir -p "$dir"
    nohup ffmpeg -loglevel error -re -f lavfi -i "$2=size=1280x720:rate=25" \
        -f lavfi -i "sine=frequency=$4:sample_rate=48000" \
        -vf "drawtext=fontfile=$FONT:text='$3':fontcolor=white:fontsize=64:box=1:boxcolor=black@0.6:boxborderw=16:x=60:y=60,drawtext=fontfile=$FONT:text='%{localtime\:%H\\\\\:%M\\\\\:%S}':fontcolor=white:fontsize=96:box=1:boxcolor=black@0.6:boxborderw=16:x=(w-tw)/2:y=h-180" \
        -c:v libx264 -preset veryfast -tune zerolatency -g 50 -keyint_min 50 -b:v 2500k -pix_fmt yuv420p \
        -c:a aac -b:a 128k -ac 2 \
        -f hls -hls_time 2 -hls_list_size 8 -hls_flags delete_segments+omit_endlist \
        "$dir/index.m3u8" > "$DATA/run/ch$n.log" 2>&1 &
    echo $! > "$pidfile"
}
channel 1 testsrc2 "Playfin Info" 440
channel 2 smptehdbars "Playfin Sport" 523
channel 3 testsrc "Playfin Cinéma" 659

docker compose -f "$HERE/compose.yml" up -d
python3 "$HERE/setup.py" "http://127.0.0.1:8097"
