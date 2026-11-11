#include "ui/theme.hpp"

#include <algorithm>
#include <cmath>

namespace playfin::ui {

namespace {
unsigned gGeneration = 1;
}

Theme& mutableTheme() {
    static Theme instance;
    return instance;
}

const std::vector<AccentChoice>& accentPalette() {
    static const std::vector<AccentChoice> palette = {
        {"playstation", {0x00, 0x70, 0xD1, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}},
        {"light", {0xFF, 0xCF, 0x8A, 0xFF}, {0x1A, 0x12, 0x05, 0xFF}},
        {"red", {0xE5, 0x48, 0x4D, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}},
        {"green", {0x30, 0xA4, 0x6C, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}},
        {"violet", {0x8E, 0x4E, 0xC6, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}},
    };
    return palette;
}

void applyAccent(const AccentChoice& choice) {
    Theme& t = mutableTheme();
    t.accent = choice.accent;
    t.onAccent = choice.onAccent;
    t.focusRing = choice.accent;
    t.controlFocused = choice.accent;
    ++gGeneration;
}

unsigned themeGeneration() { return gGeneration; }

namespace {

constexpr Color rgb(std::uint32_t hex) {
    return {static_cast<std::uint8_t>(hex >> 16), static_cast<std::uint8_t>(hex >> 8), static_cast<std::uint8_t>(hex), 0xFF};
}

// `cyrillic`, `vietnamese`: what the files cover (checked against their glyphs by fonts_test)
ThemeFont font(const char* family, const char* regular, const char* semiBold, const char* bold, bool cyrillic,
               bool vietnamese) {
    ThemeFont f;
    f.family = family;
    f.files[0] = regular;
    f.files[1] = semiBold;
    f.files[2] = bold;
    f.cyrillic = cyrillic;
    f.vietnamese = vietnamese;
    return f;
}

// the colours shared by every theme's surfaces
void surfaces(Theme& t, std::uint32_t background, std::uint32_t surface, std::uint32_t raised, std::uint32_t focused,
              std::uint32_t text, std::uint32_t secondary, std::uint32_t control) {
    t.background = rgb(background);
    t.gradientTop = t.background;
    t.gradientBottom = t.background;
    t.surface = rgb(surface);
    t.surfaceRaised = rgb(raised);
    t.surfaceFocused = rgb(focused);
    t.text = rgb(text);
    t.textSecondary = rgb(secondary);
    t.control = rgb(control);
}

std::vector<ThemeChoice> makeThemes() {
    const Color white = rgb(0xFFFFFF);
    std::vector<ThemeChoice> themes;
    // Projecteur: the identity as it is (Theme's defaults)
    themes.push_back({"projecteur", "Projecteur", Theme{}, accentPalette().front()});

    // ElegantFin (lscambo13/ElegantFin): slate gradient, indigo, pills, bordered cards with a sheen
    Theme slate;
    slate.id = "slate";
    slate.font = font("Inter", "Inter-Regular.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf", true, true);
    surfaces(slate, 0x111827, 0x1E2836, 0x263244, 0x31373F, 0xD1D5DB, 0x9CA3AF, 0x1E2836);
    slate.backgroundKind = BackgroundKind::Gradient;
    slate.gradientTop = rgb(0x1D2635);
    slate.gradientBottom = rgb(0x111827);
    slate.radius = 16;
    slate.focusStyle = FocusStyle::Ring;
    slate.focusSheen = true;
    slate.cardBorderWidth = 2;
    slate.cardShadow = 0.35f;
    slate.cardBorder = rgb(0x31373F);
    slate.tabStyle = TabStyle::Pill;
    themes.push_back({"slate", "ElegantFin", slate, {"theme", rgb(0x5D55E7), white}, "lscambo13",
                      "github.com/lscambo13/ElegantFin", "GPL-2.0"});

    // Ultrachromic (CTalvio/Ultrachromic): the focused item's backdrop blurred behind near-black glass, a glow
    Theme chroma;
    chroma.id = "chroma";
    chroma.font = font("PlusJakartaSans", "PlusJakartaSans-Regular.ttf", "PlusJakartaSans-SemiBold.ttf",
                       "PlusJakartaSans-Bold.ttf", false, true);
    surfaces(chroma, 0x080808, 0x141414, 0x1E1E1E, 0x2A2A2A, 0xFFFFFF, 0x9E9E9E, 0x141414);
    chroma.backgroundKind = BackgroundKind::Plain;
    chroma.backdropKind = BackdropKind::Blur;
    chroma.backdropBrightness = 0.40f;
    chroma.backdropSaturation = 1.2f;
    chroma.backdropContrast = 1.2f;
    chroma.backdropFollowsFocus = true;
    chroma.glowStrength = 0.75f;
    chroma.tabStyle = TabStyle::Plain;
    themes.push_back({"chroma", "Ultrachromic", chroma, {"theme", rgb(0x6279CD), white}, "Casper Talvio",
                      "github.com/CTalvio/Ultrachromic", "MIT"});

    // Abyss (AumGupta/abyss-jellyfin): near black and white, a dim glow from the backdrop, outlined cards
    Theme abyss;
    abyss.id = "abyss";
    abyss.font = font("Figtree", "Figtree-Regular.ttf", "Figtree-SemiBold.ttf", "Figtree-Bold.ttf", false, false);
    surfaces(abyss, 0x121212, 0x2A2A2A, 0x333333, 0x3D3D3D, 0xF5F5F7, 0x9A9A9C, 0x2A2A2A);
    abyss.backgroundKind = BackgroundKind::Plain;
    abyss.backdropKind = BackdropKind::Blur;
    abyss.backdropBrightness = 0.25f;
    abyss.backdropFollowsFocus = true;
    abyss.radius = 14;
    abyss.focusStyle = FocusStyle::Ring;
    abyss.focusInAccent = false;
    abyss.focusColor = white;
    abyss.cardBorderWidth = 1.5f;
    abyss.cardBorder = withAlpha(white, 0x47);  // 28 %
    abyss.tabStyle = TabStyle::Pill;
    abyss.navBackground = withAlpha(rgb(0x2A2A2A), 0x9E);  // 62 %
    themes.push_back({"abyss", "Abyss", abyss, {"theme", rgb(0xF5F5F7), rgb(0x121212)}, "Om Gupta",
                      "github.com/AumGupta/abyss-jellyfin", "MIT"});

    // Zesty (stpnwf/ZestyTheme): mint on green-grey, a tinted backdrop with grain, pills
    Theme mint;
    mint.id = "mint";
    // Quicksand reserves its name: its own static files, unmodified (Medium stands for SemiBold, which it has not)
    mint.font = font("Quicksand", "Quicksand-Regular.ttf", "Quicksand-Medium.ttf", "Quicksand-Bold.ttf", false, true);
    surfaces(mint, 0x151C19, 0x1B2421, 0x202925, 0x40534C, 0xE6F4EE, 0x8FA79E, 0x1B2421);
    mint.backgroundKind = BackgroundKind::Grain;
    mint.backdropKind = BackdropKind::Tint;
    mint.backdropFollowsFocus = true;
    mint.radius = 14;
    mint.focusStyle = FocusStyle::Ring;
    mint.tabStyle = TabStyle::Pill;
    mint.tabPillInAccent = true;
    mint.navBackground = withAlpha(rgb(0x40534C), 0x8C);  // 55 %
    // its licence file names Casper Talvio: both are credited
    themes.push_back({"mint", "Zesty", mint, {"theme", rgb(0xBBF1DD), rgb(0x151C19)}, "stpnwf, Casper Talvio",
                      "github.com/stpnwf/ZestyTheme", "MIT"});

    // Finimalism (tedhinklater/finimalism): the backdrop sharp, faded into deep violet along a diagonal
    Theme violet;
    violet.id = "violet";
    violet.font = font("Outfit", "Outfit-Regular.ttf", "Outfit-SemiBold.ttf", "Outfit-Bold.ttf", false, false);
    surfaces(violet, 0x110E1E, 0x1C1830, 0x241F3D, 0x2E2850, 0xFFFFFF, 0xA39FB8, 0x241F3D);
    violet.backgroundKind = BackgroundKind::Plain;
    violet.backdropKind = BackdropKind::Diagonal;
    violet.backdropFollowsFocus = true;
    violet.radius = 13;
    violet.focusStyle = FocusStyle::Frame;
    violet.cardShadow = 0.45f;
    violet.focusInAccent = false;
    violet.focusColor = white;
    themes.push_back({"violet", "Finimalism", violet, {"theme", rgb(0x8564FF), white}, "tedhinklater",
                      "github.com/tedhinklater/finimalism", "GPL-3.0"});
    return themes;
}

double linear(std::uint8_t channel) {
    const double c = channel / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(Color c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }

}  // namespace

const std::vector<ThemeChoice>& themeChoices() {
    static const std::vector<ThemeChoice> themes = makeThemes();
    return themes;
}

const ThemeChoice& themeById(const std::string& id) {
    for (const ThemeChoice& choice : themeChoices()) {
        if (id == choice.id) {
            return choice;
        }
    }
    return themeChoices().front();
}

void applyTheme(const ThemeChoice& choice) {
    mutableTheme() = choice.tokens;
    applyAccent(choice.accent);  // bumps the generation
}

double contrastRatio(Color a, Color b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace playfin::ui
