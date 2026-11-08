#pragma once

#include <cstdint>
#include <vector>

#include "ui/theme.hpp"

struct NVGcontext;

namespace playfin::ui {

// A vertical fade of one colour: alpha `from` on the first row to `to` at row `ramp` (sampled at the pixel's
// centre, as NanoVG's linear gradient), `to` below. Premultiplied RGBA, width x height.
std::vector<std::uint8_t> verticalFadeImage(int width, int height, int ramp, Color color, std::uint8_t from,
                                            std::uint8_t to);

// The same as an image of the canvas, made once and kept (made again when the theme changes). The software
// renderer draws a gradient through its general per-pixel shader, several times slower than an image drawn 1:1:
// large fades drawn every frame (the hints' fade, the shade over a backdrop) use this.
int verticalFadeSprite(NVGcontext* vg, int width, int height, int ramp, Color color, std::uint8_t from,
                       std::uint8_t to);

// Draws it at (x, y) on whole pixels.
void drawVerticalFade(NVGcontext* vg, float x, float y, int width, int height, int ramp, Color color,
                      std::uint8_t from, std::uint8_t to);

}  // namespace playfin::ui
