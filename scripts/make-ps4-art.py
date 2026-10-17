#!/usr/bin/env python3
"""Makes the PS4 home screen art of Playfin from the shipped Sora font (needs Pillow).

    python3 scripts/make-ps4-art.py

icon0.png (512x512): "Playfin." under a blue halo. pic1.png (1920x1080): the app's background with the halo and the
logo, shown by the PS4 behind the selected app. Colours from the theme (src/ui/theme.cpp).
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BACKGROUND = (0x0E, 0x0E, 0x11)
ACCENT = (0x00, 0x70, 0xD1)
TEXT = (0xF2, 0xF2, 0xF5)
DOT = (0x2F, 0x8C, 0xFF)  # lighter than the accent: legible at icon size
FONT = os.path.join(ROOT, "resources", "fonts", "Sora-Bold.ttf")


def halo(width, height, centre_y, radius_x, radius_y, strength):
    """Background with an elliptical light of the accent (strength at the centre, gone at 70 % of the radii)."""
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    cx, cy = width / 2, centre_y * height
    for y in range(height):
        for x in range(width):
            dx = (x + 0.5 - cx) / (radius_x * width)
            dy = (y + 0.5 - cy) / (radius_y * height)
            d = math.sqrt(dx * dx + dy * dy)
            light = 0.0 if d >= 0.7 else strength * (1 - d / 0.7)
            pixels[x, y] = tuple(round(b + (a - b) * light) for a, b in zip(ACCENT, BACKGROUND))
    return image


def logo(image, size, centre_y):
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, size)
    name_width = draw.textlength("Playfin", font=font)
    dot_width = draw.textlength(".", font=font)
    x = (image.width - name_width - dot_width) / 2
    draw.text((x, centre_y), "Playfin", font=font, fill=TEXT, anchor="lm")
    draw.text((x + name_width, centre_y), ".", font=font, fill=DOT, anchor="lm")


def main():
    out = os.path.join(ROOT, "resources", "ps4", "sce_sys")
    icon = halo(512, 512, 0.0, 0.9, 0.9, 0.6)
    logo(icon, 104, 270)
    icon.save(os.path.join(out, "icon0.png"))
    picture = halo(1920, 1080, -0.1, 0.7, 0.55, 0.3)
    logo(picture, 160, 330)
    picture.save(os.path.join(out, "pic1.png"))


if __name__ == "__main__":
    main()
