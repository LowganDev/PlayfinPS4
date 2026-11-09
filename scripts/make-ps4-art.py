#!/usr/bin/env python3
"""Makes the PS4 home screen art and the README logo of Playfin from the shipped Sora font (needs Pillow).

    python3 scripts/make-ps4-art.py

icon0.png (512x512) and pic1.png (1920x1080, shown by the PS4 behind the selected app): two play triangles with
soft edges, purple behind and blue in front (fast forward), in Jellyfin's purple-to-blue gradient, with "Playfin." and
"JELLYFIN CLIENT FOR PS4" over them. Jellyfin's branding guide forbids its logo inside another logo without
permission, allows its gradient on a shape of our own and its name to say what the app works with: only the
gradient and the name are used. docs/images/logo-on-dark.png and logo-on-light.png: the "Playfin." wordmark on a
transparent background for the README (GitHub's dark and light themes). docs/images/download.png: the README's download button. Colours from the theme (src/ui/theme.cpp).
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BACKGROUND = (0x0E, 0x0E, 0x11)
ACCENT = (0x00, 0x70, 0xD1)
TEXT = (0xF2, 0xF2, 0xF5)
DOT = (0x2F, 0x8C, 0xFF)  # lighter than the accent: legible at icon size
GRADIENT = ((0xAA, 0x5C, 0xC3), (0x00, 0xA4, 0xDC))  # Jellyfin's, left to right, for the home screen art
FONT = os.path.join(ROOT, "resources", "fonts", "Sora-Bold.ttf")
SUBTITLE_FONT = os.path.join(ROOT, "resources", "fonts", "Sora-SemiBold.ttf")
SUBTITLE = "JELLYFIN CLIENT FOR PS4"
PURPLE_DEEP = (0x7A, 0x44, 0xB8)


def solid(width, height, colour):
    return Image.new("RGB", (width, height), colour)


def gradient(width, height, left, right, angle=-25):
    """left to right, turned by `angle` degrees"""
    mask = Image.linear_gradient("L").resize((512, 512)).rotate(90 + angle, resample=Image.BICUBIC)
    mask = ImageChops.invert(mask.crop((106, 106, 406, 406)).resize((width, height)))
    return Image.composite(solid(width, height, right), solid(width, height, left), mask)


def triangle(width, height, left, centre_y, size_y, size_x, feather):
    """A play triangle (its upright side at x = left, its point at left + size_x), corners slightly rounded and
    edges blurred by `feather`."""
    mask = Image.new("L", (width * 2, height * 2), 0)
    ImageDraw.Draw(mask).polygon([(left * 2, (centre_y - size_y / 2) * 2), ((left + size_x) * 2, centre_y * 2),
                                  (left * 2, (centre_y + size_y / 2) * 2)], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(size_x * 0.06)).point(lambda v: 255 if v > 127 else 0)
    return mask.resize((width, height), Image.LANCZOS).filter(ImageFilter.GaussianBlur(feather))


def title(width, height, centre_y, size):
    """"Playfin." and the subtitle in spaced capitals, centred, on a transparent layer with a soft shadow"""
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    font = ImageFont.truetype(FONT, size)
    small = ImageFont.truetype(SUBTITLE_FONT, round(size * 0.24))
    spacing = small.size * 0.12
    name_width = draw.textlength("Playfin", font=font)
    dot_width = draw.textlength(".", font=font)
    sub_width = sum(draw.textlength(ch, font=small) for ch in SUBTITLE) + spacing * (len(SUBTITLE) - 1)
    ascent, descent = font.getmetrics()
    block = size * 1.1 + size * 0.14 + small.size * 1.1
    baseline = centre_y - block / 2 - size * 0.06 + ascent
    x = (width - name_width - dot_width) / 2
    draw.text((x, baseline), "Playfin", font=font, fill=TEXT, anchor="ls")
    draw.text((x + name_width, baseline), ".", font=font, fill=GRADIENT[1], anchor="ls")
    sub_baseline = baseline + descent + size * 0.14 + small.getmetrics()[0] * 0.8
    sx = (width - sub_width) / 2
    for ch in SUBTITLE:
        draw.text((sx, sub_baseline), ch, font=small, fill=TEXT, anchor="ls")
        sx += draw.textlength(ch, font=small) + spacing
    shadow = layer.split()[3].filter(ImageFilter.GaussianBlur(size * 0.12)).point(lambda v: v * 60 // 100)
    return layer, shadow


def home_art(width, height, left, centre_y, size_y, size_x, name_size):
    """The two triangles, purple behind and blue in front, and the title over them"""
    image = solid(width, height, BACKGROUND)
    feather = size_x * 0.035
    back = triangle(width, height, left, centre_y, size_y * 0.86, size_x * 0.86, feather)
    image.paste(gradient(width, height, GRADIENT[0], PURPLE_DEEP), (0, 0), back.point(lambda v: v * 50 // 100))
    front = triangle(width, height, left + size_x * 0.22, centre_y, size_y * 0.86, size_x * 0.86, feather)
    image.paste(gradient(width, height, GRADIENT[1], GRADIENT[0]), (0, 0), front.point(lambda v: v * 62 // 100))
    layer, shadow = title(width, height, centre_y, name_size)
    image.paste(solid(width, height, (0, 0, 0)), (0, 0), shadow)
    image.paste(layer, (0, 0), layer)
    return image


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
    home_art(512, 512, 36, 256, 470, 430, 80).save(os.path.join(out, "icon0.png"))
    home_art(1920, 1080, 560, 540, 940, 840, 160).save(os.path.join(out, "pic1.png"))
    images = os.path.join(ROOT, "docs", "images")
    os.makedirs(images, exist_ok=True)
    wordmark(TEXT, DOT).save(os.path.join(images, "logo-on-dark.png"))
    wordmark(BACKGROUND, ACCENT).save(os.path.join(images, "logo-on-light.png"))
    download_button().save(os.path.join(images, "download.png"))


if __name__ == "__main__":
    main()
