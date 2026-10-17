#include "ui/halo.hpp"

#include <algorithm>
#include <cmath>

namespace playfin::ui {

std::vector<std::uint8_t> haloImage(int width, int height, Color background, Color accent) {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(std::max(width, 0)) *
                                    static_cast<std::size_t>(std::max(height, 0)) * 4);
    const double cx = 0.5 * width;
    const double cy = -0.1 * height;
    const double rx = 0.7 * width;
    const double ry = 0.55 * height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = (x + 0.5 - cx) / rx;
            const double dy = (y + 0.5 - cy) / ry;
            const double d = std::sqrt(dx * dx + dy * dy);
            // CSS radial-gradient(accent 30 % at 0, transparent at 70 %) of the radii
            const double light = d >= 0.7 ? 0.0 : 0.3 * (1.0 - d / 0.7);
            std::uint8_t* p = &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x)) * 4];
            const auto mix = [light](std::uint8_t from, std::uint8_t to) {
                return static_cast<std::uint8_t>(std::lround(from + (to - from) * light));
            };
            p[0] = mix(background.r, accent.r);
            p[1] = mix(background.g, accent.g);
            p[2] = mix(background.b, accent.b);
            p[3] = 255;
        }
    }
    return image;
}

int haloLitRows(int height) {
    // centre at -10 %, light gone at 70 % of the 55 % radius: -0.1 + 0.385 = 0.285 of the height
    return std::min(height, static_cast<int>(std::ceil(0.285 * height)) + 1);
}

std::vector<std::uint8_t> glowImage(int width, int height, float radius, float feather, Color accent, float strength) {
    const int margin = static_cast<int>(feather);
    const int w = std::max(width, 0) + 2 * margin;
    const int h = std::max(height, 0) + 2 * margin;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const double ex = width / 2.0;
    const double ey = height / 2.0;
    const double r = std::min<double>(radius, std::min(ex, ey));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // NanoVG's sdroundrect at the pixel centre, relative to the picture's centre
            const double px = std::fabs(x + 0.5 - w / 2.0) - (ex - r);
            const double py = std::fabs(y + 0.5 - h / 2.0) - (ey - r);
            const double outside = std::sqrt(std::max(px, 0.0) * std::max(px, 0.0) + std::max(py, 0.0) * std::max(py, 0.0));
            const double distance = std::min(std::max(px, py), 0.0) + outside - r;
            const double d = std::clamp((distance + feather * 0.5) / feather, 0.0, 1.0);
            const double alpha = strength * (1.0 - d);
            std::uint8_t* p = &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                      static_cast<std::size_t>(x)) * 4];
            const auto premultiplied = [alpha](std::uint8_t channel) {
                return static_cast<std::uint8_t>(std::lround(channel * alpha));
            };
            p[0] = premultiplied(accent.r);
            p[1] = premultiplied(accent.g);
            p[2] = premultiplied(accent.b);
            p[3] = static_cast<std::uint8_t>(std::lround(255 * alpha));
        }
    }
    return image;
}

}  // namespace playfin::ui
