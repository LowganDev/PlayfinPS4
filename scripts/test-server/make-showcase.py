#!/usr/bin/env python3
"""The test server's showcase media (SHOWCASE=1 up.sh), for the README's screenshots and the console tests: real
albums under free licences from the Internet Archive with their covers, and real TV channels that their
broadcasters stream for free, with their logos (Wikimedia Commons) and their real programmes (xmltvfr.fr). Credits
in scripts/showcase/CREDITS.md. Usage: make-showcase.py DATA (the albums and logos are fetched once, the guide when
it is older than 6 hours)."""
import io
import json
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import xml.sax.saxutils as su

from PIL import Image

DATA = sys.argv[1]
STREAMS = os.environ.get("STREAMS", "streams")
AGENT = {"User-Agent": "PlayfinTestServer/1.0 (https://github.com/LowganDev/PlayfinPS4)"}
MUSIC = os.path.join(DATA, "media", "music")
LIVE = os.path.join(DATA, "media", "livetv")
LOGOS = os.path.join(DATA, "hls", "logos")

# (Internet Archive item, artist, album, year, genre, cover file in the item)
ALBUMS = [
    ("nineinchnails_ghosts_I_IV", "Nine Inch Nails", "Ghosts I–IV", 2008, "Ambient", "cover.jpg"),
    ("nine_inch_nails_the_slip", "Nine Inch Nails", "The Slip", 2008, "Rock", "cover.jpg"),
    ("OpenGoldbergVariations", "Kimiko Ishizaka", "The Open Goldberg Variations", 2012, "Classical", "cover.jpg"),
    ("pandacd-715-js-bach-the-art-of-the-fugue-kunst-der-fuge-bwv-1080", "Kimiko Ishizaka", "The Art of the Fugue",
     2017, "Classical", None),  # its cover is the item's 3000x3000 JPEG
    ("JoshWoodward-Ashes", "Josh Woodward", "Ashes", 2010, "Folk rock", "cover-13.jpg"),
]

# (guide id in xmltvfr.fr, name, number, the broadcaster's own stream, logo on Wikimedia Commons)
CHANNELS = [
    ("France24.fr", "France 24", 1, "https://live.france24.com/hls/live/2037179/F24_FR_HI_HLS/master_5000.m3u8",
     "File:France 24 logo (2013).svg"),
    ("Arte.fr", "Arte", 2, "https://artesimulcast.akamaized.net/hls/live/2031003/artelive_fr/index.m3u8",
     "File:Arte Logo 2017.svg"),
    ("Euronews.fr", "Euronews", 3,
     "https://2f6c5bf4.wurl.com/master/f36d25e7e52f1ba8d7e56eb859c636563214f541/"
     "UmxheHhUVi1ldV9FdXJvbmV3c0ZyYW5jYWlzX0hMUw/playlist.m3u8",
     "File:Euronews 2022.svg"),
    ("TV5MondeInfo.fr", "TV5Monde Info", 4, "https://ott.tv5monde.com/Content/HLS/Live/channel(info)/variant.m3u8",
     "File:Logo TV5Monde 2025.svg"),
]
GUIDE_URL = "https://xmltvfr.fr/xmltv/xmltv.xml"


def get(url, timeout=120):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=timeout) as response:
        return response.read()


def fetch(url, path):
    for attempt in range(3):
        try:
            data = get(url)
            with open(path + ".part", "wb") as out:
                out.write(data)
            os.replace(path + ".part", path)
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2)


def tags(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format_tags", "-of", "json", path],
                         capture_output=True, text=True, check=True).stdout
    return {k.lower(): v for k, v in json.loads(out).get("format", {}).get("tags", {}).items()}


def album(item, artist, name, year, genre, cover):
    folder = os.path.join(MUSIC, artist, name)
    if os.path.exists(os.path.join(folder, ".done")):
        return
    os.makedirs(folder, exist_ok=True)
    meta = json.loads(get(f"https://archive.org/metadata/{item}"))
    files = meta["files"]
    if cover is None:
        cover = max((f for f in files if f.get("format") == "JPEG"), key=lambda f: int(f.get("size", 0)))["name"]
    picture = Image.open(io.BytesIO(get(f"https://archive.org/download/{item}/{urllib.parse.quote(cover)}")))
    picture.convert("RGB").resize((1000, 1000), Image.LANCZOS).save(os.path.join(folder, "cover.jpg"), quality=90)
    tracks = {}  # one VBR MP3 per original file (some items have two, "x.mp3" and "x_vbr.mp3")
    for f in sorted((f for f in files if f.get("format") == "VBR MP3"), key=lambda f: f["name"]):
        tracks.setdefault(f.get("original") or f["name"], f)
    tracks = sorted(tracks.values(), key=lambda f: f["name"])
    for number, entry in enumerate(tracks, 1):
        target = os.path.join(folder, f"{number:02d}.mp3")
        if os.path.exists(target):
            continue
        source = target + ".source"
        fetch(f"https://archive.org/download/{item}/{urllib.parse.quote(entry['name'])}", source)
        title = tags(source).get("title") or os.path.splitext(entry["name"])[0]
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", source, "-map", "0:a", "-c", "copy",
                        "-map_metadata", "-1", "-metadata", f"title={title}", "-metadata", f"artist={artist}",
                        "-metadata", f"album_artist={artist}", "-metadata", f"album={name}",
                        "-metadata", f"track={number}", "-metadata", f"date={year}",
                        "-metadata", f"genre={genre}", target], check=True)
        os.remove(source)
    open(os.path.join(folder, ".done"), "w").close()
    print(f"album {artist} - {name}: {len(tracks)} tracks")


def commons_png(title, width):
    query = {"action": "query", "titles": title, "prop": "imageinfo", "iiprop": "url", "iiurlwidth": str(width),
             "format": "json"}
    pages = json.loads(get("https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(query)))
    return next(iter(pages["query"]["pages"].values()))["imageinfo"][0]["thumburl"]


def logo(number, title):
    path = os.path.join(LOGOS, f"{number}.png")
    if os.path.exists(path):
        return
    mark = Image.open(io.BytesIO(get(commons_png(title, 400)))).convert("RGBA")
    if mark.height > mark.width:  # Arte's file is drawn upright, reading upwards
        mark = mark.rotate(-90, expand=True)
    mark.thumbnail((200, 200), Image.LANCZOS)
    tile = Image.new("RGBA", (256, 256), (255, 255, 255, 255))  # a white tile: every logo stays readable
    tile.alpha_composite(mark, ((256 - mark.width) // 2, (256 - mark.height) // 2))
    tile.convert("RGB").save(path)
    print(f"logo {number}: {title}")


def guide():
    path = os.path.join(LIVE, "guide.xml")
    if os.path.exists(path) and time.time() - os.path.getmtime(path) < 6 * 3600:
        return
    text = get(GUIDE_URL, timeout=600).decode("utf-8")
    wanted = {c[0] for c in CHANNELS}
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<tv generator-info-name="playfin-showcase (xmltvfr.fr)">']
    for cid, name, number, _, _ in CHANNELS:
        lines.append(f'  <channel id="{cid}"><display-name>{su.escape(name)}</display-name>'
                     f'<display-name>{number}</display-name></channel>')
    count = 0
    for match in re.finditer(r'<programme [^>]*channel="([^"]+)"[^>]*>.*?</programme>', text, re.S):
        if match.group(1) in wanted:
            lines.append("  " + match.group(0))
            count += 1
    lines.append("</tv>")
    with open(path + ".part", "w", encoding="utf-8") as out:
        out.write("\n".join(lines) + "\n")
    os.replace(path + ".part", path)
    print(f"guide: {count} programmes")


def playlist():
    lines = ["#EXTM3U"]
    for cid, name, number, url, _ in CHANNELS:
        lines.append(f'#EXTINF:-1 tvg-id="{cid}" tvg-chno="{number}" tvg-name="{name}" '
                     f'tvg-logo="http://{STREAMS}/logos/{number}.png" group-title="News",{name}')
        lines.append(url)
    with open(os.path.join(LIVE, "channels.m3u"), "w", encoding="utf-8") as out:
        out.write("\n".join(lines) + "\n")


def main():
    for folder in (MUSIC, LIVE, LOGOS, os.path.join(DATA, "media", "films")):
        os.makedirs(folder, exist_ok=True)
    for entry in ALBUMS:
        album(*entry)
    for _, _, number, _, title in CHANNELS:
        logo(number, title)
    playlist()
    guide()
    print(f"showcase media ready in {DATA}/media")


main()
