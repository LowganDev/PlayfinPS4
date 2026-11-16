#include <doctest/doctest.h>

#include <algorithm>
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

namespace {
// back to Projecteur and its accent after a test
struct ThemeGuard {
    ~ThemeGuard() { applyTheme(themeById("projecteur")); }
};
}  // namespace

TEST_CASE("seven themes, in order, each a complete set of colours") {
    const auto& themes = themeChoices();
    REQUIRE(themes.size() == 7);
    const char* ids[] = {"projecteur", "slate", "chroma", "abyss", "mint", "violet", "daylight"};
    const char* names[] = {"Projecteur", "ElegantFin", "Ultrachromic", "Abyss", "Zesty", "Finimalism", "Plein jour"};
    for (std::size_t i = 0; i < themes.size(); ++i) {
        CHECK(std::string(themes[i].id) == ids[i]);
        CHECK(std::string(themes[i].name) == names[i]);
        CHECK(themes[i].tokens.id == ids[i]);
        CHECK(themes[i].tokens.background.a == 255);
        CHECK(themes[i].tokens.surface.a == 255);
        REQUIRE(themes[i].tokens.font.family != nullptr);
    }
    CHECK(std::string(themeById("nope").id) == "projecteur");
    CHECK(std::string(themeById("violet").tokens.font.family) == "Outfit");
}

TEST_CASE("Projecteur is today's look") {
    const Theme& t = themeById("projecteur").tokens;
    const Theme today;
    CHECK(same(t.background, today.background));
    CHECK(same(t.textSecondary, today.textSecondary));
    CHECK(t.radius == doctest::Approx(12));
    CHECK(t.backgroundKind == BackgroundKind::Halo);
    CHECK(t.backdropKind == BackdropKind::Shade);
    CHECK(t.focusStyle == FocusStyle::Glow);
    CHECK(t.tabStyle == TabStyle::Underline);
    CHECK(std::string(t.font.family) == "Sora");
    CHECK(same(themeById("projecteur").accent.accent, accentPalette().front().accent));
}

TEST_CASE("Plein jour is light, our own (no credit)") {
    const ThemeChoice& choice = themeById("daylight");
    REQUIRE(std::string(choice.id) == "daylight");
    CHECK(isLightGround(choice.tokens.background));
    CHECK(contrastRatio(choice.tokens.text, choice.tokens.background) >= 15.0);  // near-black on near-white
    CHECK(choice.author == nullptr);
    CHECK(choice.source == nullptr);
    CHECK(choice.license == nullptr);
    for (const ThemeChoice& other : themeChoices()) {
        CHECK((std::string(other.id) == "daylight") == isLightGround(other.tokens.background));
    }
}

TEST_CASE("every theme's text reads on its surfaces") {
    for (const auto& t : themeChoices()) {
        const std::string id = t.id;
        CAPTURE(id);
        CHECK(contrastRatio(t.tokens.text, t.tokens.background) >= 7.0);
        CHECK(contrastRatio(t.tokens.text, t.tokens.surface) >= 4.5);
        // the player's panels
        CHECK(contrastRatio(t.tokens.text, t.tokens.surfaceRaised) >= 4.5);
        CHECK(contrastRatio(t.tokens.textSecondary, t.tokens.background) >= 4.5);
        // settings rows draw their values in the dim colour on the control colour
        CHECK(contrastRatio(t.tokens.textSecondary, t.tokens.surface) >= 4.5);
        CHECK(contrastRatio(t.tokens.textSecondary, t.tokens.control) >= 4.5);
        // text on an accent fill is always large (24 px SemiBold rows and buttons, the avatar's initial): WCAG's
        // large-text level
        CHECK(contrastRatio(t.accent.onAccent, t.accent.accent) >= 3.0);
    }
    CHECK(contrastRatio(Color{0, 0, 0, 255}, Color{255, 255, 255, 255}) == doctest::Approx(21.0));
}

TEST_CASE("a theme brings its own accent and bumps the generation") {
    ThemeGuard guard;
    const unsigned before = themeGeneration();
    applyTheme(themeById("mint"));
    CHECK(themeGeneration() != before);
    CHECK(theme().id == "mint");
    CHECK(same(theme().accent, Color{0xBB, 0xF1, 0xDD, 0xFF}));
    CHECK(same(theme().focusRing, theme().accent));
    CHECK(same(theme().background, Color{0x15, 0x1C, 0x19, 0xFF}));
    applyTheme(themeById("projecteur"));
    CHECK(same(theme().accent, Color{0x00, 0x70, 0xD1, 0xFF}));
}

TEST_CASE("the accent's marks on a light theme are darkened until they show, and are the accent elsewhere") {
    ThemeGuard guard;
    for (const AccentChoice& a : accentPalette()) {
        CAPTURE(std::string(a.id));
        applyTheme(themeById("daylight"));
        applyAccent(a);
        CHECK(same(theme().accent, a.accent));  // fills keep the user's colour
        CHECK(contrastRatio(theme().focusRing, theme().background) >= 3.0);
        CHECK(contrastRatio(theme().focusRing, theme().surface) >= 3.0);  // the ring drawn round a white card or row
        if (contrastRatio(a.accent, theme().background) >= 3.0) {
            CHECK(same(theme().focusRing, a.accent));
        } else {  // the same hue, darker: no channel grows, their order is kept
            CHECK(theme().focusRing.r <= a.accent.r);
            CHECK(theme().focusRing.g <= a.accent.g);
            CHECK(theme().focusRing.b <= a.accent.b);
            CHECK((theme().focusRing.r >= theme().focusRing.b) == (a.accent.r >= a.accent.b));
        }
        for (const ThemeChoice& dark : themeChoices()) {
            if (std::string(dark.id) == "daylight") {
                continue;
            }
            CAPTURE(std::string(dark.id));
            applyTheme(dark);
            applyAccent(a);
            CHECK(same(theme().focusRing, a.accent));
        }
    }
    applyTheme(themeById("daylight"));
    CHECK(same(theme().focusRing, theme().accent));  // its own accent, PlayStation blue, shows as it is
}

TEST_CASE("the player's dark stage: Projecteur's colours on a light theme, nothing on a dark one") {
    ThemeGuard guard;
    const Theme projecteur;
    applyTheme(themeById("daylight"));
    applyAccent(accentPalette()[1]);  // the pale "light" accent
    const Theme light = theme();
    unsigned generation = themeGeneration();
    {
        DarkStage stage;
        CHECK(darkStage());
        CHECK(themeGeneration() != generation);
        CHECK(same(theme().background, projecteur.background));
        CHECK(same(theme().surfaceRaised, projecteur.surfaceRaised));
        CHECK(same(theme().text, projecteur.text));
        CHECK(same(theme().textSecondary, projecteur.textSecondary));
        CHECK(same(theme().scrim, projecteur.scrim));
        CHECK(same(theme().accent, light.accent));  // the user's accent stays
        CHECK(same(theme().focusRing, light.accent));  // on a dark ground it needs no darkening
        CHECK(theme().id == "daylight");
        {
            DarkStage nested;  // the next episode's player, built before the last one goes
            CHECK(darkStage());
        }
        CHECK(darkStage());
        CHECK(same(theme().background, projecteur.background));
        generation = themeGeneration();
    }
    CHECK_FALSE(darkStage());
    CHECK(themeGeneration() != generation);
    CHECK(same(theme().background, light.background));
    CHECK(same(theme().text, light.text));
    CHECK(same(theme().focusRing, light.focusRing));

    applyTheme(themeById("slate"));
    const Theme slate = theme();
    generation = themeGeneration();
    {
        DarkStage stage;
        CHECK_FALSE(darkStage());
        CHECK(themeGeneration() == generation);
        CHECK(same(theme().background, slate.background));
    }
    CHECK(themeGeneration() == generation);

    applyTheme(themeById("daylight"));
    {
        DarkStage stage;
        applyTheme(themeById("mint"));  // a theme chosen meanwhile is kept when the stage ends
    }
    CHECK(theme().id == "mint");
    CHECK_FALSE(darkStage());
}

TEST_CASE("theme backgrounds: Projecteur's halo, ElegantFin's gradient, nothing for a plain one, a fixed grain") {
    const Theme& projecteur = themeById("projecteur").tokens;
    Theme lit = projecteur;
    lit.accent = accentPalette().front().accent;
    CHECK(backgroundImage(192, 108, lit) == haloImage(192, 108, lit.background, lit.accent));
    CHECK(backgroundLitRows(108, lit) == haloLitRows(108));

    const Theme& slate = themeById("slate").tokens;
    const std::vector<std::uint8_t> gradient = backgroundImage(64, 100, slate);
    REQUIRE(gradient.size() == 64u * 100u * 4u);
    const auto pixel = [&](const std::vector<std::uint8_t>& image, int x, int y) {
        const std::size_t at = (static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)) * 4;
        return Color{image[at], image[at + 1], image[at + 2], image[at + 3]};
    };
    const auto near = [](Color a, Color b) {
        return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1 && a.a == 255;
    };
    CHECK(near(pixel(gradient, 10, 0), slate.gradientTop));
    CHECK(near(pixel(gradient, 50, 99), slate.gradientBottom));
    CHECK(backgroundLitRows(100, slate) == 100);

    CHECK(backgroundImage(64, 100, themeById("chroma").tokens).empty());
    CHECK(backgroundLitRows(100, themeById("chroma").tokens) == 0);

    const Theme& mint = themeById("mint").tokens;
    const std::vector<std::uint8_t> grain = backgroundImage(64, 64, mint);
    CHECK(grain == backgroundImage(64, 64, mint));  // the same every time (stable screenshots)
    long sum[3] = {0, 0, 0};
    bool varies = false;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const Color c = pixel(grain, x, y);
            sum[0] += c.r;
            sum[1] += c.g;
            sum[2] += c.b;
            varies = varies || c.g != mint.background.g;
        }
    }
    CHECK(varies);
    CHECK(std::abs(sum[0] / 4096 - mint.background.r) <= 2);
    CHECK(std::abs(sum[1] / 4096 - mint.background.g) <= 2);
    CHECK(std::abs(sum[2] / 4096 - mint.background.b) <= 2);
}

TEST_CASE("a card's border as an image: a ring on the picture's edge, rounded at the corners, empty inside") {
    const Color colour{0x31, 0x37, 0x3F, 0xFF};
    const int w = 40;
    const int h = 30;
    const std::vector<std::uint8_t> ring = ringImage(w, h, 8, 2, colour);
    REQUIRE(ring.size() == static_cast<std::size_t>(w * h * 4));
    const auto alpha = [&](int x, int y) { return ring[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4 + 3]; };
    CHECK(alpha(20, 0) > 200);   // the top edge
    CHECK(alpha(20, 1) > 200);
    CHECK(alpha(20, 3) == 0);    // inside
    CHECK(alpha(20, 15) == 0);
    CHECK(alpha(0, 15) > 200);   // the left edge
    CHECK(alpha(0, 0) == 0);     // outside the rounded corner
    CHECK(ring[(static_cast<std::size_t>(0) * w + 20) * 4] <= alpha(20, 0));  // premultiplied
}

TEST_CASE("ElegantFin's sheen on the focused card is a light touch") {
    const float strength = themeById("slate").tokens.sheenStrength;
    CHECK(strength > 0);
    CHECK(strength <= 0.1f);
    const std::vector<std::uint8_t> sheen = sheenImage(200, 120, strength);
    int peak = 0;
    for (std::size_t i = 3; i < sheen.size(); i += 4) {
        peak = std::max(peak, int(sheen[i]));
    }
    CHECK(peak <= int(255 * strength) + 1);
    CHECK(peak > 0);
}

TEST_CASE("ElegantFin's and Finimalism's cards cast a soft shadow; the other themes' do not") {
    CHECK(themeById("slate").tokens.cardShadow > 0);
    CHECK(themeById("violet").tokens.cardShadow > 0);
    for (const char* id : {"projecteur", "chroma", "abyss", "mint"}) {
        CAPTURE(std::string(id));
        CHECK(themeById(id).tokens.cardShadow == doctest::Approx(0));
    }
}
