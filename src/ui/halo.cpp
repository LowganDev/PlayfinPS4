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

namespace {

std::uint8_t clampByte(int value) { return static_cast<std::uint8_t>(std::min(255, std::max(0, value))); }

}  // namespace

void addGrain(std::vector<std::uint8_t>& rgba, int width, int height) {
    std::uint32_t state = 0x2545F491u;  // fixed: the same grain on every run
    const std::size_t count = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    for (std::size_t i = 0; i < count && i * 4 + 3 < rgba.size(); ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const int delta = static_cast<int>(state % 13) - 6;  // -6 to 6, the same on each channel (no colour noise)
        for (int c = 0; c < 3; ++c) {
            rgba[i * 4 + static_cast<std::size_t>(c)] = clampByte(rgba[i * 4 + static_cast<std::size_t>(c)] + delta);
        }
    }
}

std::vector<std::uint8_t> backgroundImage(int width, int height, const Theme& theme) {
    switch (theme.backgroundKind) {
    case BackgroundKind::Halo:
        return haloImage(width, height, theme.background, theme.accent);
    case BackgroundKind::Plain:
        return {};
    case BackgroundKind::Gradient:
    case BackgroundKind::Grain:
        break;
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(std::max(width, 0)) *
                                    static_cast<std::size_t>(std::max(height, 0)) * 4);
    const bool gradient = theme.backgroundKind == BackgroundKind::Gradient;
    for (int y = 0; y < height; ++y) {
        const double t = gradient && height > 1 ? static_cast<double>(y) / (height - 1) : 0.0;
        const Color from = gradient ? theme.gradientTop : theme.background;
        const Color to = gradient ? theme.gradientBottom : theme.background;
        const auto mix = [t](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(std::lround(a + (b - a) * t)); };
        const std::uint8_t row[4] = {mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), 255};
        for (int x = 0; x < width; ++x) {
            std::copy(row, row + 4, &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                            static_cast<std::size_t>(x)) * 4]);
        }
    }
    if (!gradient) {
        addGrain(image, width, height);
    }
    return image;
}

std::vector<std::uint8_t> ringImage(int width, int height, float radius, float ringWidth, Color color) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0)) * 4, 0);
    const double r = std::min<double>(radius, std::min(width, height) / 2.0);
    const double hw = width / 2.0 - r;
    const double hh = height / 2.0 - r;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // signed distance to the rounded rectangle's edge, positive inside
            const double qx = std::fabs(x + 0.5 - width / 2.0) - hw;
            const double qy = std::fabs(y + 0.5 - height / 2.0) - hh;
            const double outside = std::sqrt(std::max(qx, 0.0) * std::max(qx, 0.0) + std::max(qy, 0.0) * std::max(qy, 0.0));
            const double inside = std::min(std::max(qx, qy), 0.0);
            const double d = r - (outside + inside);
            const double cover = std::min(1.0, std::max(0.0, d + 0.5)) - std::min(1.0, std::max(0.0, d - ringWidth + 0.5));
            if (cover <= 0) {
                continue;
            }
            const double a = cover * color.a / 255.0;
            std::uint8_t* p = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            p[0] = clampByte(static_cast<int>(std::lround(color.r * a)));
            p[1] = clampByte(static_cast<int>(std::lround(color.g * a)));
            p[2] = clampByte(static_cast<int>(std::lround(color.b * a)));
            p[3] = clampByte(static_cast<int>(std::lround(255 * a)));
        }
    }
    return out;
}

std::vector<std::uint8_t> sheenImage(int width, int height, float strength) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0)) * 4, 0);
    const double angle = 115.0 * 3.14159265358979 / 180.0;
    const double dx = std::sin(angle);
    const double dy = -std::cos(angle);
    const double length = std::fabs(width * dx) + std::fabs(height * dy);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double t = ((x + 0.5 - width / 2.0) * dx + (y + 0.5 - height / 2.0) * dy) / length + 0.5;
            const double a = strength * std::max(0.0, 1.0 - std::fabs(t - 0.45) / 0.12);
            const std::uint8_t v = clampByte(static_cast<int>(std::lround(255 * a)));
            std::uint8_t* p = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            p[0] = p[1] = p[2] = p[3] = v;
        }
    }
    return out;
}

int backgroundLitRows(int height, const Theme& theme) {
    switch (theme.backgroundKind) {
    case BackgroundKind::Halo:
        return haloLitRows(height);
    case BackgroundKind::Plain:
        return 0;
    case BackgroundKind::Gradient:
    case BackgroundKind::Grain:
        break;
    }
    return height;
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
