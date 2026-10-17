#!/usr/bin/env python3
"""Makes the PS4 home screen art and the README logo of Playfin from the shipped Sora font (needs Pillow).

    python3 scripts/make-ps4-art.py

icon0.png (512x512): "Playfin." under a halo in Jellyfin's purple-to-blue gradient. pic1.png (1920x1080): the same
halo and logo, shown by the PS4 behind the selected app. Jellyfin's branding guide forbids its logo inside another
logo without permission and allows its gradient on a shape of our own: only the gradient is used. docs/images/logo-on-dark.png and logo-on-light.png: the "Playfin."
wordmark on a transparent background for the README (GitHub's dark and light themes). docs/images/download.png: the
README's download button. Colours from the theme (src/ui/theme.cpp).
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BACKGROUND = (0x0E, 0x0E, 0x11)
ACCENT = (0x00, 0x70, 0xD1)
TEXT = (0xF2, 0xF2, 0xF5)
DOT = (0x2F, 0x8C, 0xFF)  # lighter than the accent: legible at icon size
GRADIENT = ((0xAA, 0x5C, 0xC3), (0x00, 0xA4, 0xDC))  # Jellyfin's, left to right, for the home screen art
FONT = os.path.join(ROOT, "resources", "fonts", "Sora-Bold.ttf")


def halo(width, height, centre_y, radius_x, radius_y, strength, colours=(ACCENT, ACCENT)):
    """Background with an elliptical light (strength at the centre, gone at 70 % of the radii) whose colour goes from
    colours[0] on the left edge to colours[1] on the right."""
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    cx, cy = width / 2, centre_y * height
    for y in range(height):
        for x in range(width):
            dx = (x + 0.5 - cx) / (radius_x * width)
            dy = (y + 0.5 - cy) / (radius_y * height)
            d = math.sqrt(dx * dx + dy * dy)
            light = 0.0 if d >= 0.7 else strength * (1 - d / 0.7)
            t = (x + 0.5) / width
            colour = tuple(l + (r - l) * t for l, r in zip(*colours))
            pixels[x, y] = tuple(round(b + (a - b) * light) for a, b in zip(colour, BACKGROUND))
    return image


def logo(image, size, centre_y, dot=DOT):
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, size)
    name_width = draw.textlength("Playfin", font=font)
    dot_width = draw.textlength(".", font=font)
    x = (image.width - name_width - dot_width) / 2
    draw.text((x, centre_y), "Playfin", font=font, fill=TEXT, anchor="lm")
    draw.text((x + name_width, centre_y), ".", font=font, fill=dot, anchor="lm")


def wordmark(text_colour, dot_colour, size=160, margin=16):
    """"Playfin." alone, cropped to its ink plus a margin, on a transparent background."""
    font = ImageFont.truetype(FONT, size)
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    name_width = probe.textlength("Playfin", font=font)
    left, top, right, bottom = probe.textbbox((0, 0), "Playfin.", font=font)
    image = Image.new("RGBA", (right - left + 2 * margin, bottom - top + 2 * margin), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    origin = (margin - left, margin - top)
    draw.text(origin, "Playfin", font=font, fill=text_colour)
    draw.text((origin[0] + name_width, origin[1]), ".", font=font, fill=dot_colour)
    return image


def download_button(scale=2):
    """"Download for PS4" with an arrow into a tray, white on the accent, drawn at `scale` for sharp screens."""
    font = ImageFont.truetype(FONT, 22 * scale)
    label = "Download for PS4"
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    text_width = probe.textlength(label, font=font)
    pad, icon, gap, height = 22 * scale, 22 * scale, 12 * scale, 56 * scale
    image = Image.new("RGBA", (round(pad + icon + gap + text_width + pad), height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, image.width - 1, height - 1), radius=height // 2, fill=ACCENT)
    stroke = round(2.6 * scale)
    left, top = pad, (height - icon) / 2
    middle = left + icon / 2
    draw.line((middle, top, middle, top + icon * 0.62), fill=TEXT, width=stroke)
    head = icon * 0.3
    draw.line((middle - head, top + icon * 0.62 - head, middle, top + icon * 0.62), fill=TEXT, width=stroke, joint="curve")
    draw.line((middle + head, top + icon * 0.62 - head, middle, top + icon * 0.62), fill=TEXT, width=stroke, joint="curve")
    draw.line((left, top + icon * 0.72, left, top + icon, left + icon, top + icon, left + icon, top + icon * 0.72),
              fill=TEXT, width=stroke, joint="curve")
    draw.text((left + icon + gap, height / 2), label, font=font, fill=TEXT, anchor="lm")
    return image


def main():
    out = os.path.join(ROOT, "resources", "ps4", "sce_sys")
    icon = halo(512, 512, 0.0, 0.9, 0.9, 0.6, GRADIENT)
    logo(icon, 104, 270, GRADIENT[1])
    icon.save(os.path.join(out, "icon0.png"))
    picture = halo(1920, 1080, -0.1, 0.7, 0.55, 0.3, GRADIENT)
    logo(picture, 160, 330, GRADIENT[1])
    picture.save(os.path.join(out, "pic1.png"))
    images = os.path.join(ROOT, "docs", "images")
    os.makedirs(images, exist_ok=True)
    wordmark(TEXT, DOT).save(os.path.join(images, "logo-on-dark.png"))
    wordmark(BACKGROUND, ACCENT).save(os.path.join(images, "logo-on-light.png"))
    download_button().save(os.path.join(images, "download.png"))


if __name__ == "__main__":
    main()
