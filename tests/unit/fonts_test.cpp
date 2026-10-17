#include <doctest/doctest.h>

#include <nanovg.h>

#include <cmath>
#include <string>

#include "render/nvg_sw.hpp"
#include "render/raster.hpp"
#include "ui/label.hpp"
#include "ui/nvg_backend.hpp"
#include "ui/text.hpp"

using namespace playfin;
using ui::FontStyle;
using ui::FontWeight;

namespace {

const std::string kFonts = std::string(PLAYFIN_SOURCE_DIR) + "/resources/fonts";

struct Canvas {
    render::Surface surface{64, 64};
    NVGcontext* vg = render::createSoftwareCanvas(surface, 1);
    ~Canvas() { render::destroySoftwareCanvas(vg); }
};

}  // namespace

TEST_CASE("font weights map to the three faces") {
    CHECK(std::string(ui::fontFace(FontWeight::Regular)) == "regular");
    CHECK(std::string(ui::fontFace(FontWeight::SemiBold)) == "semibold");
    CHECK(std::string(ui::fontFace(FontWeight::Bold)) == "bold");
}

TEST_CASE("Sora is the typeface, except for languages whose script it lacks") {
    CHECK(ui::primaryFamily("fr") == std::string("Sora"));
    CHECK(ui::primaryFamily("ja") == std::string("Sora"));
    CHECK(ui::primaryFamily("ru") == std::string("NotoSans"));
    CHECK(ui::primaryFamily("uk") == std::string("NotoSans"));
    CHECK(ui::primaryFamily("vi") == std::string("NotoSans"));
}

TEST_CASE("every script measures, through the fallbacks, in every weight") {
    Canvas canvas;
    REQUIRE(canvas.vg != nullptr);
    ui::NanoVgText text(canvas.vg);
    REQUIRE(text.loadFonts(kFonts, "fr"));
    for (FontWeight weight : {FontWeight::Regular, FontWeight::SemiBold, FontWeight::Bold}) {
        const FontStyle style{24, weight};
        CHECK(text.measure("Bonjour", style) > 0);
        CHECK(text.measure("Привет", style) > 0);
        CHECK(text.measure("日本語", style) > 0);
        CHECK(text.measure("★ 9.0", style) > text.measure(" 9.0", style));
    }
    CHECK(text.measure("Bonjour", FontStyle{24, FontWeight::Bold}) >
          text.measure("Bonjour", FontStyle{24, FontWeight::Regular}));
}

TEST_CASE("Sora and Noto Sans give different widths (Sora is really loaded)") {
    Canvas sora;
    Canvas noto;
    ui::NanoVgText soraText(sora.vg);
    ui::NanoVgText notoText(noto.vg);
    REQUIRE(soraText.loadFonts(kFonts, "fr"));
    REQUIRE(notoText.loadFonts(kFonts, "ru"));
    CHECK(soraText.measure("Reprendre", FontStyle{}) != doctest::Approx(notoText.measure("Reprendre", FontStyle{})));
}

namespace {
// the vertical centre of the ink a one-line label draws in its line box, minus the box's centre (px)
float inkOffset(const std::string& language, const std::string& text, float size) {
    render::Surface surface{240, 240};
    NVGcontext* vg = render::createSoftwareCanvas(surface, 1);
    float offset = 1000;
    {
        ui::NanoVgText metrics(vg);
        REQUIRE(metrics.loadFonts(kFonts, language));
        ui::Label label(text, FontStyle{size, FontWeight::Bold});
        label.color = ui::Color{255, 255, 255, 255};
        const float height = metrics.lineHeight(label.style);
        label.place({0, 0, 240, height}, metrics);
        surface.clear(0, 0, 0, 255);
        nvgBeginFrame(vg, 240, 240, 1);
        nvgTranslate(vg, 0, 60);
        label.draw(vg);
        nvgEndFrame(vg);
        int top = -1;
        int bottom = -1;
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 240; ++x) {
                if (surface.rgba[(static_cast<std::size_t>(y) * 240 + static_cast<std::size_t>(x)) * 4] > 128) {
                    top = top < 0 ? y : top;
                    bottom = y;
                }
            }
        }
        REQUIRE(top >= 0);
        offset = (static_cast<float>(top + bottom + 1) / 2 - 60) - height / 2;
    }
    render::destroySoftwareCanvas(vg);
    return offset;
}
}  // namespace

TEST_CASE("a line of capitals, digits or symbols sits in the middle of its line box") {
    for (const char* language : {"fr", "ru"}) {  // Sora, Noto Sans
        for (const char* text : {"H", "+", "8"}) {
            CAPTURE(language);
            CAPTURE(text);
            CHECK(std::abs(inkOffset(language, text, 72)) <= 1.5f);
            CHECK(std::abs(inkOffset(language, text, 24)) <= 1.0f);
        }
    }
}
