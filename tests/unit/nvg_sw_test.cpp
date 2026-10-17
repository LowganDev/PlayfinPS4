#include <doctest/doctest.h>

#include <nanovg.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "render/nvg_sw.hpp"
#include "render/raster.hpp"

using namespace playfin;

namespace {

struct Canvas {
    render::Surface surface{64, 64};
    NVGcontext* vg = render::createSoftwareCanvas(surface);
    ~Canvas() { render::destroySoftwareCanvas(vg); }
    void begin() {
        surface.clear(0, 0, 0, 255);
        nvgBeginFrame(vg, 64, 64, 1);
    }
    void end() { nvgEndFrame(vg); }
    const std::uint8_t* at(int x, int y) const {
        return &surface.rgba[(static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)) * 4];
    }
    int redInRow(int y) const {
        int count = 0;
        for (int x = 0; x < 64; ++x) {
            count += at(x, y)[0] > 0 ? 1 : 0;
        }
        return count;
    }
    int redInColumn(int x) const {
        int count = 0;
        for (int y = 0; y < 64; ++y) {
            count += at(x, y)[0] > 0 ? 1 : 0;
        }
        return count;
    }
    bool stencilClear() const {
        for (std::uint8_t value : surface.stencil) {
            if (value != 0) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace

TEST_CASE("a filled rectangle is solid inside, anti-aliased at its edges, and drawn once") {
    Canvas c;
    REQUIRE(c.vg != nullptr);
    c.begin();
    nvgBeginPath(c.vg);
    nvgRect(c.vg, 10, 10, 20, 20);
    nvgFillColor(c.vg, nvgRGBA(255, 0, 0, 255));
    nvgFill(c.vg);
    c.end();
    CHECK(c.at(20, 20)[0] == 255);
    CHECK(c.at(5, 5)[0] == 0);
    CHECK(c.at(40, 40)[0] == 0);
    CHECK(c.redInRow(20) >= 20);
    CHECK(c.redInRow(20) <= 22);
    CHECK(c.redInRow(20) == c.redInColumn(20));
    CHECK(c.stencilClear());
}

TEST_CASE("a circle is solid in the middle with partially covered edge pixels") {
    Canvas c;
    c.begin();
    nvgBeginPath(c.vg);
    nvgCircle(c.vg, 32, 32, 20);
    nvgFillColor(c.vg, nvgRGBA(255, 0, 0, 255));
    nvgFill(c.vg);
    c.end();
    CHECK(c.at(32, 32)[0] == 255);
    CHECK(c.at(2, 2)[0] == 0);
    int partial = 0;
    for (int x = 0; x < 64; ++x) {
        const int r = c.at(x, 32)[0];
        partial += (r > 0 && r < 255) ? 1 : 0;
    }
    CHECK(partial >= 2);
}

TEST_CASE("holes are left empty (non-zero winding through the stencil)") {
    Canvas c;
    c.begin();
    nvgBeginPath(c.vg);
    nvgRect(c.vg, 8, 8, 48, 48);
    nvgRect(c.vg, 24, 24, 16, 16);
    nvgPathWinding(c.vg, NVG_HOLE);
    nvgFillColor(c.vg, nvgRGBA(255, 0, 0, 255));
    nvgFill(c.vg);
    c.end();
    CHECK(c.at(14, 32)[0] == 255);
    CHECK(c.at(32, 32)[0] == 0);
    CHECK(c.at(2, 32)[0] == 0);
    CHECK(c.stencilClear());
}

TEST_CASE("translucent strokes are not blended twice where they cross") {
    Canvas c;
    c.begin();
    nvgBeginPath(c.vg);
    nvgMoveTo(c.vg, 10, 10);
    nvgLineTo(c.vg, 54, 54);
    nvgMoveTo(c.vg, 54, 10);
    nvgLineTo(c.vg, 10, 54);
    nvgStrokeColor(c.vg, nvgRGBA(255, 0, 0, 128));
    nvgStrokeWidth(c.vg, 6);
    nvgStroke(c.vg);
    c.end();
    const int single = c.at(16, 16)[0];
    const int crossing = c.at(32, 32)[0];
    CHECK(single > 100);
    CHECK(std::abs(single - crossing) <= 2);
    CHECK(c.stencilClear());
}

TEST_CASE("images are created, updated and drawn through an image pattern") {
    Canvas c;
    const unsigned char texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const int image = nvgCreateImageRGBA(c.vg, 2, 2, NVG_IMAGE_NEAREST, texels);
    REQUIRE(image > 0);
    int w = 0;
    int h = 0;
    nvgImageSize(c.vg, image, &w, &h);
    CHECK(w == 2);
    CHECK(h == 2);
    c.begin();
    nvgBeginPath(c.vg);
    nvgRect(c.vg, 0, 0, 64, 64);
    nvgFillPaint(c.vg, nvgImagePattern(c.vg, 0, 0, 64, 64, 0, image, 1));
    nvgFill(c.vg);
    c.end();
    CHECK(c.at(16, 16)[0] == 255);
    CHECK(c.at(48, 16)[1] == 255);
    CHECK(c.at(16, 48)[2] == 255);
    CHECK(c.at(48, 48)[0] == 255);
    const unsigned char black[16] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    nvgUpdateImage(c.vg, image, black);
    c.begin();
    nvgBeginPath(c.vg);
    nvgRect(c.vg, 0, 0, 64, 64);
    nvgFillPaint(c.vg, nvgImagePattern(c.vg, 0, 0, 64, 64, 0, image, 1));
    nvgFill(c.vg);
    c.end();
    CHECK(c.at(16, 16)[0] == 0);
    nvgDeleteImage(c.vg, image);
}

TEST_CASE("text is drawn from the font atlas") {
    Canvas c;
    const std::string font = std::string(PLAYFIN_SOURCE_DIR) + "/resources/fonts/NotoSans-Regular.ttf";
    REQUIRE(nvgCreateFont(c.vg, "regular", font.c_str()) >= 0);
    c.begin();
    nvgFontFace(c.vg, "regular");
    nvgFontSize(c.vg, 32);
    nvgFillColor(c.vg, nvgRGBA(255, 255, 255, 255));
    nvgText(c.vg, 4, 40, "Hi", nullptr);
    c.end();
    int lit = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            lit += c.at(x, y)[0] > 128 ? 1 : 0;
        }
    }
    CHECK(lit > 40);
    CHECK(c.at(60, 60)[0] == 0);
}

TEST_CASE("drawing in bands on several threads gives exactly the single-thread frame") {
    render::Surface one(200, 150);
    render::Surface four(200, 150);
    NVGcontext* single = render::createSoftwareCanvas(one, 1);
    NVGcontext* banded = render::createSoftwareCanvas(four, 4);
    REQUIRE(single != nullptr);
    REQUIRE(banded != nullptr);
    for (NVGcontext* vg : {single, banded}) {
        render::Surface& s = vg == single ? one : four;
        s.clear(0, 0, 0, 255);
        nvgBeginFrame(vg, 200, 150, 1);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 10, 10, 180, 130, 24);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 200, 150, nvgRGBA(255, 0, 0, 255), nvgRGBA(0, 0, 255, 128)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, 100, 75, 50);
        nvgPathWinding(vg, NVG_HOLE);
        nvgCircle(vg, 100, 75, 20);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 0, 200));
        nvgStrokeWidth(vg, 7);
        nvgStroke(vg);
        nvgFillColor(vg, nvgRGBA(0, 255, 0, 160));
        nvgFill(vg);
        nvgEndFrame(vg);
    }
    CHECK(one.rgba == four.rgba);
    CHECK(one.stencil == four.stencil);
    render::destroySoftwareCanvas(single);
    render::destroySoftwareCanvas(banded);
}

TEST_CASE("an image deleted while the frame is recorded is still drawn by that frame") {
    Canvas c;
    std::uint8_t green[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        green[i * 4] = 0;
        green[i * 4 + 1] = 255;
        green[i * 4 + 2] = 0;
        green[i * 4 + 3] = 255;
    }
    const int image = nvgCreateImageRGBA(c.vg, 4, 4, 0, green);
    REQUIRE(image > 0);
    c.begin();
    nvgBeginPath(c.vg);
    nvgRect(c.vg, 0, 0, 16, 16);
    nvgFillPaint(c.vg, nvgImagePattern(c.vg, 0, 0, 16, 16, 0, image, 1));
    nvgFill(c.vg);
    nvgDeleteImage(c.vg, image);
    c.end();
    CHECK(c.at(8, 8)[1] == 255);
    int w = 0;
    int h = 0;
    nvgImageSize(c.vg, image, &w, &h);
    CHECK(w == 0);  // gone after the frame
}
