#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "render/composite.hpp"

using namespace playfin;

namespace {

const std::uint8_t* at(const std::vector<std::uint8_t>& image, int width, int x, int y) {
    return &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
}

}  // namespace

TEST_CASE("letterboxing keeps the video's aspect ratio, centred") {
    const render::Box wide = render::letterbox(1920, 800, 1920, 1080);
    CHECK(wide.x == doctest::Approx(0));
    CHECK(wide.width == doctest::Approx(1920));
    CHECK(wide.height == doctest::Approx(800));
    CHECK(wide.y == doctest::Approx(140));
    const render::Box tall = render::letterbox(720, 1080, 1920, 1080);
    CHECK(tall.height == doctest::Approx(1080));
    CHECK(tall.width == doctest::Approx(720));
    CHECK(tall.x == doctest::Approx(600));
    const render::Box none = render::letterbox(0, 0, 1920, 1080);
    CHECK(none.width == doctest::Approx(0));
}

TEST_CASE("the test composite puts the UI over the letterboxed video") {
    std::vector<std::uint8_t> video(4 * 2 * 4, 0);
    for (std::size_t i = 0; i < video.size(); i += 4) {
        video[i] = 200;  // red, rgb0
    }
    std::vector<std::uint8_t> ui(8 * 8 * 4, 0);  // transparent
    ui[0] = ui[1] = ui[2] = ui[3] = 255;         // (0,0) opaque white
    std::uint8_t* half = &ui[(4 * 8 + 4) * 4];   // (4,4) 50 % black, premultiplied
    half[3] = 128;
    platform::VideoLayer layer{video.data(), 4, 2, true};
    std::vector<std::uint8_t> out;
    render::compositeForTest(out, 8, 8, layer, ui);
    REQUIRE(out.size() == 8u * 8u * 4u);
    CHECK(at(out, 8, 0, 0)[0] == 255);   // UI on top
    CHECK(at(out, 8, 0, 0)[1] == 255);
    CHECK(at(out, 8, 3, 1)[0] == 0);     // bar above the video (video rows 2-5)
    CHECK(at(out, 8, 3, 3)[0] == 200);   // video
    CHECK(at(out, 8, 3, 3)[1] == 0);
    CHECK(at(out, 8, 4, 4)[0] == 100);   // video under 50 % black
    CHECK(at(out, 8, 3, 7)[0] == 0);     // bar below
    CHECK(at(out, 8, 3, 7)[3] == 255);
}
