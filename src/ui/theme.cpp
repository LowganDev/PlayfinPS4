#include "ui/theme.hpp"

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

}  // namespace playfin::ui
