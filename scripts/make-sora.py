#!/usr/bin/env python3
"""Makes the static Sora fonts shipped in resources/fonts from the variable font of the google/fonts repository.

stb_truetype (used by NanoVG) reads only a variable font's default instance, so each weight is instanced.
Usage (once, needs fontTools):
    curl -L -o Sora.ttf "https://github.com/google/fonts/raw/main/ofl/sora/Sora%5Bwght%5D.ttf"
    python3 scripts/make-sora.py Sora.ttf resources/fonts
"""
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

WEIGHTS = {"Regular": 400, "SemiBold": 600, "Bold": 700}


def main(source, out_dir):
    for name, weight in WEIGHTS.items():
        font = instantiateVariableFont(TTFont(source), {"wght": weight}, updateFontNames=True)
        font.save(f"{out_dir}/Sora-{name}.ttf")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
