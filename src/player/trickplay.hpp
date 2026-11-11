#pragma once

#include <optional>
#include <vector>

#include "jellyfin/models.hpp"

namespace playfin::player {

// Where the picture for a time is: the sheet's number and its rectangle in the sheet, in pixels.
struct TrickplayFrame {
    int sheet = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The picture for `seconds` (clamped to the first and last pictures).
TrickplayFrame trickplayFrame(const jellyfin::TrickplayInfo& info, double seconds);
// How many sheets the pictures fill.
int trickplaySheets(const jellyfin::TrickplayInfo& info);
// The image decoder's limits (ui::kMaxImageSide, ui::kMaxImagePixels): a bigger sheet would never show.
constexpr int kMaxSheetSide = 4096;
constexpr long long kMaxSheetPixels = 8LL * 1024 * 1024;

// The width to show on a 1080p TV, among those whose sheets the decoder takes: the smallest at least 240 pixels
// wide, else the largest; none when there is none.
std::optional<jellyfin::TrickplayInfo> pickTrickplay(const std::vector<jellyfin::TrickplayInfo>& widths);

}  // namespace playfin::player
