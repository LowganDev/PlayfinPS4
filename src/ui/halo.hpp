#pragma once

#include <cstdint>
#include <vector>

#include "ui/theme.hpp"

namespace playfin::ui {

// The identity's background: `background` with an elliptical light of `accent` from above the top centre (centre
// at 50 % / -10 %, radii 70 % x 55 % of the size, 30 % of the accent fading out at 70 % of the radii). Opaque RGBA,
// drawn 1:1 (the renderer copies its rows).
std::vector<std::uint8_t> haloImage(int width, int height, Color background, Color accent);

// Rows of a halo image of `height` that differ from the background (the light ends at 28.5 % of the height): only
// these need drawing over a screen cleared to the background colour.
int haloLitRows(int height);

// A theme's background, drawn 1:1 over a screen cleared to its background colour: Halo, its halo (accent from
// the theme); Gradient, gradientTop to gradientBottom down the height; Grain, the background with a fixed light
// noise (the same every time); Plain: empty, nothing to draw.
std::vector<std::uint8_t> backgroundImage(int width, int height, const Theme& theme);
// The rows of backgroundImage() that differ from the cleared background (Halo: haloLitRows; Plain: 0).
int backgroundLitRows(int height, const Theme& theme);
// Adds the fixed grain to an opaque RGBA image, in place (Zesty's background and backdrops).
void addGrain(std::vector<std::uint8_t>& rgba, int width, int height);

// The focus glow of a picture of width x height: `accent` at `strength` under a rounded rectangle (radius), fading
// out over `feather` px around it, like NanoVG's box gradient. Premultiplied RGBA of (width + 2 feather) x
// (height + 2 feather): drawn as an image (a live box gradient costs ~200 cycles per pixel on the PS4).
std::vector<std::uint8_t> glowImage(int width, int height, float radius, float feather, Color accent, float strength);

}  // namespace playfin::ui
