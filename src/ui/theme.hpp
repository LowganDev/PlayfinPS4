#pragma once

#include <cstdint>
#include <vector>

namespace playfin::ui {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

constexpr Color withAlpha(Color color, std::uint8_t alpha) { return {color.r, color.g, color.b, alpha}; }

// Design tokens of the Playfin identity: widgets read these, never
// hard-coded values.
struct Theme {
    Color background{0x0E, 0x0E, 0x11, 0xFF};
    Color surface{0x1E, 0x1F, 0x25, 0xFF};
    Color surfaceRaised{0x27, 0x28, 0x30, 0xFF};
    Color surfaceFocused{0x33, 0x35, 0x3F, 0xFF};
    Color text{0xF2, 0xF2, 0xF5, 0xFF};
    Color textSecondary{0x9A, 0x9C, 0xA8, 0xFF};
    Color accent{0x00, 0x70, 0xD1, 0xFF};  // the accent setting (PlayStation blue by default)
    Color onAccent{0xFF, 0xFF, 0xFF, 0xFF};
    Color focusRing{0x00, 0x70, 0xD1, 0xFF};  // follows the accent
    Color control{0x1E, 0x1F, 0x25, 0xFF};    // buttons and fields
    Color controlFocused{0x00, 0x70, 0xD1, 0xFF};
    Color scrim{0x00, 0x00, 0x00, 0xB0};
    Color error{0xE5, 0x48, 0x4D, 0xFF};
    float focusRingWidth = 4;
    float radius = 12;
    float spacingS = 8;
    float spacingM = 16;
    float spacingL = 24;
    float spacingXl = 32;
    float spacingXxl = 48;
    float marginSide = 96;
    float rowGap = 40;
    float cardGap = 28;
    float fontSmall = 20;    // meta: card subtitles, hints
    float fontBody = 24;     // text, buttons
    float fontTitle = 28;    // screen subtitles, player title
    float fontHeading = 30;  // row titles
    float fontLogo = 40;
    float fontDisplay = 84;  // details title
    float focusMs = 250;
    float pushMs = 350;      // a pushed screen fades in and rises
    float popMs = 250;       // the screen revealed by pop, or a replaced one, fades in
    float revealMs = 350;    // rows arriving on a screen
    float revealGapMs = 35;  // between two arriving rows
    float revealRise = 30;
    float scrollMs = 400;
    float fadeMs = 150;
};

// The accent setting's choices.
struct AccentChoice {
    const char* id;
    Color accent;
    Color onAccent;
};
const std::vector<AccentChoice>& accentPalette();
// Sets the accent and what follows it (focus ring, focused controls), and bumps themeGeneration().
void applyAccent(const AccentChoice& choice);
// Changes whenever the theme does: views and images derived from it (the halo) rebuild when it differs.
unsigned themeGeneration();

// Defined in theme.cpp, not inline: the static of an inline function gets a section of its own, and the PS4
// packaging tool (create-fself) dropped that section's contents (the console saw an all-zero theme).
Theme& mutableTheme();
inline const Theme& theme() { return mutableTheme(); }

}  // namespace playfin::ui
