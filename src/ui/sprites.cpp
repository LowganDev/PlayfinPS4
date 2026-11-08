#include "ui/sprites.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>

namespace playfin::ui {

std::vector<std::uint8_t> verticalFadeImage(int width, int height, int ramp, Color color, std::uint8_t from,
                                            std::uint8_t to) {
    width = std::max(width, 0);
    height = std::max(height, 0);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const float t =
            ramp > 0 ? std::clamp((static_cast<float>(y) + 0.5f) / static_cast<float>(ramp), 0.0f, 1.0f) : 1.0f;
        const float alpha = (from + (to - from) * t) / 255.0f;
        const auto premultiplied = [alpha](std::uint8_t channel) {
            return static_cast<std::uint8_t>(std::lround(channel * alpha));
        };
        const std::uint8_t pixel[4] = {premultiplied(color.r), premultiplied(color.g), premultiplied(color.b),
                                       static_cast<std::uint8_t>(std::lround(alpha * 255))};
        std::uint8_t* row = &pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4];
        for (int x = 0; x < width; ++x) {
            std::copy(pixel, pixel + 4, row + x * 4);
        }
    }
    return pixels;
}

int verticalFadeSprite(NVGcontext* vg, int width, int height, int ramp, Color color, std::uint8_t from,
                       std::uint8_t to) {
    struct Entry {
        NVGcontext* vg;
        int width, height, ramp;
        Color color;
        std::uint8_t from, to;
        unsigned generation;
        int image;
    };
    static std::vector<Entry> cache;
    for (Entry& entry : cache) {
        if (entry.vg == vg && entry.width == width && entry.height == height && entry.ramp == ramp &&
            entry.color.r == color.r && entry.color.g == color.g && entry.color.b == color.b &&
            entry.from == from && entry.to == to) {
            if (entry.generation == themeGeneration()) {
                return entry.image;
            }
            nvgDeleteImage(vg, entry.image);
            entry = cache.back();
            cache.pop_back();
            break;
        }
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::vector<std::uint8_t> pixels = verticalFadeImage(width, height, ramp, color, from, to);
    const int image = nvgCreateImageRGBA(vg, width, height, NVG_IMAGE_PREMULTIPLIED, pixels.data());
    if (image > 0) {
        cache.push_back({vg, width, height, ramp, color, from, to, themeGeneration(), image});
    }
    return image;
}

void drawVerticalFade(NVGcontext* vg, float x, float y, int width, int height, int ramp, Color color,
                      std::uint8_t from, std::uint8_t to) {
    const int image = verticalFadeSprite(vg, width, height, ramp, color, from, to);
    if (image <= 0) {
        return;
    }
    const float left = std::round(x);
    const float top = std::round(y);
    nvgBeginPath(vg);
    nvgRect(vg, left, top, static_cast<float>(width), static_cast<float>(height));
    nvgFillPaint(vg, nvgImagePattern(vg, left, top, static_cast<float>(width), static_cast<float>(height), 0, image, 1));
    nvgFill(vg);
}

}  // namespace playfin::ui
