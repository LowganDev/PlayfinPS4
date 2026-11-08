#include <doctest/doctest.h>

#include "ui/images.hpp"
#include "ui/sprites.hpp"

using namespace playfin;

TEST_CASE("a vertical fade: premultiplied, from transparent to its alpha over the ramp, even below") {
    const ui::Color ground{16, 32, 64, 255};
    const auto pixels = ui::verticalFadeImage(3, 10, 4, ground, 0, 200);
    REQUIRE(pixels.size() == 3 * 10 * 4);
    const auto alphaAt = [&](std::size_t x, std::size_t y) { return pixels[(y * 3 + x) * 4 + 3]; };
    CHECK(alphaAt(0, 0) == 25);   // the first row's centre: 1/8 of the way
    CHECK(alphaAt(2, 0) == 25);   // every column alike
    CHECK(alphaAt(0, 3) == 175);  // 7/8
    CHECK(alphaAt(0, 4) == 200);  // past the ramp
    CHECK(alphaAt(0, 9) == 200);
    const std::size_t last = (9 * 3) * 4;
    CHECK(pixels[last] == 13);      // 16 * 200 / 255
    CHECK(pixels[last + 1] == 25);  // 32 * 200 / 255
    CHECK(pixels[last + 2] == 50);  // 64 * 200 / 255
    CHECK(ui::verticalFadeImage(0, 5, 2, ground, 0, 255).empty());
}


TEST_CASE("a backdrop's shade is baked in: as the shade blended over it, then the ground below 70 %") {
    const int width = 2;
    const int height = 10;  // split at row 7
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4), 200);
    ui::shadeBackdrop(rgba, width, height, 14, 14, 17);
    CHECK(ui::backdropSplit(height) == 7);
    const auto at = [&](std::size_t y, std::size_t c) { return static_cast<int>(rgba[y * width * 4 + c]); };
    // row 0: alpha (143 + 112 * 0.5 / 7) / 255 = 0.592: 200 * 0.408 + 14 * 0.592 = 89.9
    CHECK(at(0, 0) == doctest::Approx(90).epsilon(0.02));
    CHECK(at(0, 2) == doctest::Approx(92).epsilon(0.02));
    CHECK(at(6, 0) < at(0, 0));  // darker down the gradient
    CHECK(at(7, 0) == 14);       // the ground from the split
    CHECK(at(9, 2) == 17);
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        CHECK(rgba[i] == 255);  // opaque: drawn as a copy of rows
    }
}
