#!/usr/bin/env python3
"""Makes the static fonts shipped in resources/fonts from a variable font of the google/fonts repository.

stb_truetype (used by NanoVG) reads only a variable font's default instance, so each weight is instanced; any
other axis (Inter's optical size) stays at its default. Usage (needs fontTools):
    curl -L -o Sora.ttf "https://github.com/google/fonts/raw/<commit>/ofl/sora/Sora%5Bwght%5D.ttf"
    python3 scripts/make-fonts.py Sora.ttf Sora resources/fonts
writes Sora-Regular.ttf, Sora-SemiBold.ttf and Sora-Bold.ttf. The commits and checksums used are listed in
THIRD_PARTY_LICENSES.
"""
import sys

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

WEIGHTS = {"Regular": 400, "SemiBold": 600, "Bold": 700}


def main(source, family, out_dir):
    axes = {axis.axisTag: axis.defaultValue for axis in TTFont(source)["fvar"].axes}
    for name, weight in WEIGHTS.items():
        location = dict(axes, wght=weight)
        font = instantiateVariableFont(TTFont(source), location, updateFontNames=True)
        font.save(f"{out_dir}/{family}-{name}.ttf")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
