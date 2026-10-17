#!/usr/bin/env bash
# Generates the test server's media in $DATA/media (default ~/.local/share/playfin-testserver): music made of
# synthesised tones (no copyrighted content) with tags and covers, channel logos, the M3U tuner list and an XMLTV
# guide for 3 days from today. The music is made once; the guide is remade on every call (up.sh).
set -euo pipefail
DATA=${DATA:-$HOME/.local/share/playfin-testserver}
HERE=$(cd "$(dirname "$0")" && pwd)
FONT="$HERE/../../resources/fonts/Sora-Bold.ttf"
MUSIC=$DATA/media/music
LIVE=$DATA/media/livetv
mkdir -p "$MUSIC" "$LIVE" "$DATA/hls/logos" "$DATA/run"

cover() {  # cover <file> <colour> <line 1> <line 2>
    ffmpeg -loglevel error -y -f lavfi -i "color=c=$2:s=600x600" -frames:v 1 \
        -vf "drawtext=fontfile=$FONT:text='$3':fontcolor=white:fontsize=56:x=(w-tw)/2:y=h/2-70,drawtext=fontfile=$FONT:text='$4':fontcolor=white@0.8:fontsize=34:x=(w-tw)/2:y=h/2+20" \
        "$1"
}

# track <dir> <ext> <codec args> <number> <title> <artist> <album artist> <album> <year> <genre> <seconds> <base Hz> [disc]
track() {
    local dir=$1 ext=$2 codec=$3 n=$4 title=$5 artist=$6 albumArtist=$7 album=$8 year=$9 genre=${10} secs=${11}
    local hz=${12} disc=${13:-1}
    local file
    file=$(printf '%s/%d-%02d %s.%s' "$dir" "$disc" "$n" "$title" "$ext")
    [ -f "$file" ] && return
    ffmpeg -loglevel error -y -f lavfi \
        -i "aevalsrc=0.18*sin(2*PI*$hz*t)*(0.6+0.4*sin(2*PI*0.5*t))+0.12*sin(2*PI*$hz*1.5*t)|0.18*sin(2*PI*$hz*1.25*t)+0.1*sin(2*PI*$hz*2*t):s=44100:d=$secs" \
        -af "afade=t=in:d=1,afade=t=out:st=$((secs - 2)):d=2" $codec \
        -metadata title="$title" -metadata artist="$artist" -metadata album_artist="$albumArtist" \
        -metadata album="$album" -metadata track="$n" -metadata disc="$disc" -metadata date="$year" \
        -metadata genre="$genre" "$file"
}

album() {  # album <artist dir> <album> <album artist> <year> <genre> <ext> <codec args> <colour> <titles…>
    local artistDir=$1 name=$2 albumArtist=$3 year=$4 genre=$5 ext=$6 codec=$7 colour=$8
    shift 8
    local dir="$MUSIC/$artistDir/$name"
    mkdir -p "$dir"
    [ -f "$dir/cover.jpg" ] || cover "$dir/cover.jpg" "$colour" "$name" "$albumArtist"
    local n=0 disc=1
    for entry in "$@"; do
        if [ "$entry" = "--disc2" ]; then disc=2; n=0; continue; fi
        n=$((n + 1))
        local title=${entry%%|*} artist=${entry#*|}
        [ "$artist" = "$entry" ] && artist=$albumArtist
        track "$dir" "$ext" "$codec" "$n" "$title" "$artist" "$albumArtist" "$name" "$year" "$genre" \
            $((30 + (n * 13) % 60)) $((220 + n * 37 + disc * 11)) "$disc"
    done
}

MP3="-c:a libmp3lame -b:a 192k"
FLAC="-c:a flac"
AAC="-c:a aac -b:a 192k"
OPUS="-c:a libopus -b:a 128k"

mkdir -p "$MUSIC/Les Ondes Calmes" "$MUSIC/Kilohertz"
[ -f "$MUSIC/Les Ondes Calmes/folder.jpg" ] || cover "$MUSIC/Les Ondes Calmes/folder.jpg" "#3B4C8F" "Les Ondes" "Calmes"
[ -f "$MUSIC/Kilohertz/folder.jpg" ] || cover "$MUSIC/Kilohertz/folder.jpg" "#8F3B6A" "Kilohertz" ""
album "Les Ondes Calmes" "Aurore synthétique" "Les Ondes Calmes" 2021 "Électronique" mp3 "$MP3" "#1F6FB2" \
    "Premier signal" "Lumière basse" "Onde porteuse" "Écho lointain" "Retour au calme" "Fréquence douce" "Dernier rayon"
album "Les Ondes Calmes" "Marée basse" "Les Ondes Calmes" 2023 "Ambient" flac "$FLAC" "#2B8C7A" \
    "Ressac" "Sel et sable" "Courant froid" "Phare" "Laisse de mer" "Étale"
album "Kilohertz" "Circuit doré" "Kilohertz" 2019 "Synthwave" m4a "$AAC" "#B2861F" \
    "Néon" "Autoroute" "Minuit" --disc2 "Rétroviseur" "Chrome" "Aube"
album "Kilohertz" "Signal faible" "Kilohertz" 2024 "Électronique" opus "$OPUS" "#6A3BB2" \
    "Antenne" "Parasites" "Bande FM" "Modulation" "Silence radio"
album "Artistes variés" "Compilation test" "Artistes variés" 2022 "Divers" mp3 "$MP3" "#B23B3B" \
    "Duo en la|Duo Sinus" "Battements|Duo Sinus" "Plage arrière|Kilohertz" "Vague lente|Les Ondes Calmes" \
    "Harmoniques|Duo Sinus" "Fin de bande|Kilohertz"

# a film with styled ASS subtitles and an attached font (the player must ignore attached fonts)
FILMS=$DATA/media/films
FILM="$FILMS/Test sous-titres (2026)/Test sous-titres (2026).mkv"
if [ ! -f "$FILM" ]; then
    mkdir -p "$(dirname "$FILM")"
    cat > "$DATA/run/subtitles.ass" <<'ASS'
[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 720

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Noto Sans,48,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,3,1,2,40,40,40,1
Style: Top,Noto Sans,40,&H0000FFFF,&H000000FF,&H00000000,&H64000000,1,0,0,0,100,100,0,0,1,3,1,8,40,40,40,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.50,0:00:10.00,Default,,0,0,0,,Bonjour, ceci est un sous-titre de test.
Dialogue: 0,0:00:00.50,0:00:10.00,Top,,0,0,0,,{\i1}Titre en haut, en italique{\i0}
Dialogue: 0,0:00:10.00,0:00:20.00,Default,,0,0,0,,{\c&H00A5FF&}Couleur{\c} et {\b1}gras{\b0} — accents : éàçù
Dialogue: 0,0:00:20.00,0:00:30.00,Default,,0,0,0,,Привет · こんにちは · 안녕하세요
ASS
    ffmpeg -loglevel error -y -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=30" \
        -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=30" -i "$DATA/run/subtitles.ass" \
        -map 0:v -map 1:a -map 2:s -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 128k -c:s ass \
        -disposition:s:0 default -metadata:s:s:0 language=fre -metadata title="Test sous-titres" \
        -attach "$FONT" -metadata:s:t mimetype=application/x-truetype-font "$FILM"
fi

# channel logos (served by the streams container) and the source of each channel
for n in 1 2 3; do
    name=$(sed -n "${n}p" <<<$'Info\nSport\nCinéma')
    colour=$(sed -n "${n}p" <<<$'#1F6FB2\n#2B8C3A\n#B23B3B')
    logo="$DATA/hls/logos/$n.png"
    [ -f "$logo" ] || ffmpeg -loglevel error -y -f lavfi -i "color=c=$colour:s=256x256" -frames:v 1 \
        -vf "drawtext=fontfile=$FONT:text='$n':fontcolor=white:fontsize=120:x=(w-tw)/2:y=(h-th)/2-30,drawtext=fontfile=$FONT:text='$name':fontcolor=white:fontsize=36:x=(w-tw)/2:y=h-70" \
        "$logo"
done

cat > "$LIVE/channels.m3u" <<'M3U'
#EXTM3U
#EXTINF:-1 tvg-id="playfin.info" tvg-chno="1" tvg-name="Playfin Info" tvg-logo="http://streams/logos/1.png" group-title="Playfin",Playfin Info
http://streams/ch1/index.m3u8
#EXTINF:-1 tvg-id="playfin.sport" tvg-chno="2" tvg-name="Playfin Sport" tvg-logo="http://streams/logos/2.png" group-title="Playfin",Playfin Sport
http://streams/ch2/index.m3u8
#EXTINF:-1 tvg-id="playfin.cinema" tvg-chno="3" tvg-name="Playfin Cinéma" tvg-logo="http://streams/logos/3.png" group-title="Playfin",Playfin Cinéma
http://streams/ch3/index.m3u8
M3U

python3 - "$LIVE/guide.xml" <<'PY'
import datetime, sys, xml.sax.saxutils as su
out = sys.argv[1]
now = datetime.datetime.now().astimezone()
start = now.replace(hour=0, minute=0, second=0, microsecond=0)
channels = [
    ("playfin.info", "Playfin Info", ["Le journal", "La météo", "Le grand débat", "Reportage", "Le point du jour"]),
    ("playfin.sport", "Playfin Sport", ["Match en direct", "Le résumé", "Magazine du sport", "Rallye", "Tennis"]),
    ("playfin.cinema", "Playfin Cinéma", ["Le film du soir", "Court métrage", "Coulisses", "Classique", "Avant-première"]),
]
lengths = [30, 45, 60, 30, 60]
def fmt(t):
    return t.strftime("%Y%m%d%H%M%S %z")
lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<tv generator-info-name="playfin-testserver">']
for cid, name, _ in channels:
    lines.append(f'  <channel id="{cid}"><display-name>{su.escape(name)}</display-name></channel>')
for c, (cid, name, titles) in enumerate(channels):
    t = start
    i = c
    end = start + datetime.timedelta(days=3)
    while t < end:
        length = datetime.timedelta(minutes=lengths[i % len(lengths)])
        title = titles[i % len(titles)]
        desc = f"{title} sur {name}, programme de test numéro {i + 1}."
        lines.append(f'  <programme start="{fmt(t)}" stop="{fmt(t + length)}" channel="{cid}">'
                     f'<title lang="fr">{su.escape(title)}</title><desc lang="fr">{su.escape(desc)}</desc>'
                     f'<category lang="fr">{"Sport" if c == 1 else "Film" if c == 2 else "Actualités"}</category>'
                     f'</programme>')
        t += length
        i += 1
lines.append('</tv>')
open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
PY
echo "media ready in $DATA/media"
