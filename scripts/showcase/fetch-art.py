#!/usr/bin/env python3
"""Fetches the free artwork of the README screenshots (the Blender Foundation's open films, from Wikimedia
Commons) into a folder, as fake_jellyfin --showcase reads it: <id>-primary.jpg and <id>-backdrop.jpg.

    python3 scripts/showcase/fetch-art.py OUT_DIR

Authors and licences: scripts/showcase/CREDITS.md."""
import json
import os
import sys
import urllib.parse
import urllib.request

FILMS = {  # id: (poster, backdrop) on Wikimedia Commons
    "sc-bbb": ("Big buck bunny poster big.jpg", "Big.Buck.Bunny.-.Frank.Rinky.Gimera.png"),
    "sc-sintel": ("Sintel poster.jpg", "Sintel-screenshot-1.jpg"),
    "sc-tos": ("Tos-poster.png", "Tos-poster.png"),
    "sc-ed": ("ElephantsDreamPoster.jpg", "Elephants Dream s3 platform.jpg"),
    "sc-cosmos": ("CosmosLaundromatPoster.jpg", "CosmosLaundromat-final-scene.jpg"),
    "sc-spring": ("Spring2019AlphaPosterBlender.jpg", "Spring-concept art-Autumn 02.png"),
    "sc-llamigos": ("Blender Foundation - Caminandes - Episode 3 - Llamigos - Cover thumbnail.png",
                    "Blender Foundation - Caminandes - Episode 3 - Llamigos - Koro and Oti share more food to eat at night.png"),
}
AGENT = {"User-Agent": "PlayfinReadmeShots/1.0 (https://github.com/LowganDev/PlayfinPS4)"}


def thumb(title, width):
    query = {"action": "query", "titles": "File:" + title, "prop": "imageinfo", "iiprop": "url",
             "iiurlwidth": str(width), "format": "json"}
    request = urllib.request.Request("https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(query),
                                     headers=AGENT)
    pages = json.load(urllib.request.urlopen(request, timeout=30))["query"]["pages"]
    return next(iter(pages.values()))["imageinfo"][0]["thumburl"]


def fetch(url, path):
    request = urllib.request.Request(url, headers=AGENT)
    with urllib.request.urlopen(request, timeout=60) as response, open(path, "wb") as out:
        out.write(response.read())


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for film, (poster, backdrop) in FILMS.items():
        for kind, title, width in (("primary", poster, 600), ("backdrop", backdrop, 1920)):
            path = os.path.join(out, f"{film}-{kind}.jpg")
            if not os.path.exists(path):
                fetch(thumb(title, width), path)
                print(path)


if __name__ == "__main__":
    main()
