#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "testing/image.hpp"

using playfin::testing::Image;

namespace {

Image solid(int w, int h, std::uint8_t value) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<std::size_t>(w * h * 4), value);
    return image;
}

}  // namespace

TEST_CASE("identical and slightly different images match") {
    const Image a = solid(10, 10, 100);
    Image b = solid(10, 10, 100);
    b.rgba[0] = 106;  // within the tolerance of 8
    const auto diff = playfin::testing::diffImages(a, b);
    CHECK(diff.sameSize);
    CHECK(diff.differing == 0);
    CHECK(playfin::testing::imagesMatch(diff));
}

TEST_CASE("a changed area or a different size fails") {
    const Image a = solid(100, 100, 0);
    Image b = solid(100, 100, 0);
    for (int i = 0; i < 20; ++i) {
        b.rgba[static_cast<std::size_t>(i * 4)] = 255;
    }
    const auto diff = playfin::testing::diffImages(a, b);
    CHECK(diff.differing == 20);
    CHECK_FALSE(playfin::testing::imagesMatch(diff));
    CHECK_FALSE(playfin::testing::imagesMatch(playfin::testing::diffImages(a, solid(100, 99, 0))));
}

TEST_CASE("PNG round trip and missing files") {
    char tmpl[] = "/tmp/playfin-img-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string path = std::string(tmpl) + "/a.png";
    Image a = solid(4, 3, 50);
    a.rgba[5] = 200;
    REQUIRE(playfin::testing::writePng(path, a));
    const auto loaded = playfin::testing::loadPng(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->width == 4);
    CHECK(loaded->height == 3);
    CHECK(loaded->rgba == a.rgba);
    CHECK_FALSE(playfin::testing::loadPng(std::string(tmpl) + "/missing.png").has_value());
    std::filesystem::remove_all(tmpl);
}
