#include <doctest/doctest.h>

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
