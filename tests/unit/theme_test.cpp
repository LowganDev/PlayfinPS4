#include <doctest/doctest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "ui/animation.hpp"
#include "ui/halo.hpp"
#include "ui/theme.hpp"

using namespace playfin::ui;

namespace {

bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// restores the default accent after a test
struct AccentGuard {
    ~AccentGuard() { applyAccent(accentPalette().front()); }
};

}  // namespace

TEST_CASE("the accent palette: PlayStation blue first, light text only on the dark accents") {
    const auto& palette = accentPalette();
    REQUIRE(palette.size() == 5);
    CHECK(std::string(palette[0].id) == "playstation");
    CHECK(same(palette[0].accent, Color{0x00, 0x70, 0xD1, 0xFF}));
    CHECK(same(palette[0].onAccent, Color{0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK(std::string(palette[1].id) == "light");
    CHECK(palette[1].onAccent.r < 0x40);  // dark text on the light gold
    CHECK(same(theme().accent, palette[0].accent));
}

TEST_CASE("applying an accent changes the theme and its generation") {
    AccentGuard guard;
    const unsigned before = themeGeneration();
    applyAccent(accentPalette()[2]);
    CHECK(same(theme().accent, accentPalette()[2].accent));
    CHECK(same(theme().onAccent, accentPalette()[2].onAccent));
    CHECK(themeGeneration() != before);
}

TEST_CASE("the identity colours") {
    const Theme t;
    CHECK(same(t.background, Color{0x0E, 0x0E, 0x11, 0xFF}));
    CHECK(same(t.surface, Color{0x1E, 0x1F, 0x25, 0xFF}));
    CHECK(same(t.surfaceRaised, Color{0x27, 0x28, 0x30, 0xFF}));
    CHECK(same(t.text, Color{0xF2, 0xF2, 0xF5, 0xFF}));
    CHECK(same(t.textSecondary, Color{0x9A, 0x9C, 0xA8, 0xFF}));
    CHECK(t.radius == doctest::Approx(12));
    CHECK(t.focusMs == doctest::Approx(250));
}

TEST_CASE("the smooth easing starts fast and settles") {
    CHECK(ease(Easing::Smooth, 0) == doctest::Approx(0));
    CHECK(ease(Easing::Smooth, 1) == doctest::Approx(1));
    const float half = ease(Easing::Smooth, 0.5f);
    CHECK(half > 0.7f);
    CHECK(half < 0.95f);
}

TEST_CASE("the halo: accent light at the top centre, plain background at the bottom") {
    const Color background{0x0E, 0x0E, 0x11, 0xFF};
    const Color accent{0x00, 0x70, 0xD1, 0xFF};
    const std::vector<std::uint8_t> image = haloImage(192, 108, background, accent);
    REQUIRE(image.size() == 192u * 108u * 4u);
    for (std::size_t i = 3; i < image.size(); i += 4) {
        REQUIRE(image[i] == 255);
    }
    const std::uint8_t* top = &image[(0 * 192 + 96) * 4];
    // at the top centre the light is ~ 30 % of the accent over the background (the centre is above the screen)
    CHECK(top[2] > background.b + 30);
    CHECK(top[2] < background.b + 0.3 * (accent.b - background.b) + 8);
    const std::uint8_t* corner = &image[(107 * 192 + 0) * 4];
    CHECK(corner[0] == background.r);
    CHECK(corner[1] == background.g);
    CHECK(corner[2] == background.b);
}

TEST_CASE("the focus glow: strongest under the picture, gone 50 px around it, premultiplied") {
    const Color accent{0x00, 0x70, 0xD1, 0xFF};
    const int w = 100;
    const int h = 60;
    const std::vector<std::uint8_t> sprite = glowImage(w, h, 12, 50, accent, 0.45f);
    const int sw = w + 100;
    const int sh = h + 100;
    REQUIRE(sprite.size() == static_cast<std::size_t>(sw * sh * 4));
    const std::uint8_t* centre = &sprite[(static_cast<std::size_t>(sh / 2) * sw + sw / 2) * 4];
    CHECK(centre[3] == 115);  // 45 % of 255
    CHECK(centre[2] <= centre[3]);
    CHECK(centre[0] == 0);
    const std::uint8_t* edge = &sprite[(static_cast<std::size_t>(sh / 2) * sw + 50) * 4];  // the picture's left edge
    CHECK(edge[3] > 40);
    CHECK(edge[3] < 75);
    const std::uint8_t* corner = &sprite[0];
    CHECK(corner[3] == 0);
}

TEST_CASE("below its lit rows the halo is the plain background") {
    const Color background{0x0E, 0x0E, 0x11, 0xFF};
    const Color accent{0x00, 0x70, 0xD1, 0xFF};
    const int width = 192;
    const int height = 108;
    const std::vector<std::uint8_t> image = haloImage(width, height, background, accent);
    const int lit = haloLitRows(height);
    CHECK(lit < height / 2);
    for (int y = lit; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = &image[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4];
            REQUIRE(p[0] == background.r);
            REQUIRE(p[1] == background.g);
            REQUIRE(p[2] == background.b);
        }
    }
    const std::uint8_t* above = &image[(static_cast<std::size_t>(lit / 2) * width + width / 2) * 4];
    CHECK(above[2] != background.b);  // light inside the lit rows
}
