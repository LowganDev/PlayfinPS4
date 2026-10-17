#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ui/theme.hpp"

namespace playfin::app {

// The user's choices, kept in settings.json in the data folder.
struct Settings {
    std::string accent = "playstation";  // id in ui::accentPalette()
    std::int64_t maxBitrate = 40000000;  // bits/s, sent as MaxStreamingBitrate
    std::string language;                // UI language tag, "" = the console's
    std::vector<std::string> recentSearches;  // newest first, at most 8
};

// Missing file: defaults. Damaged file: renamed .bad, defaults. Unknown or invalid values: their defaults.
Settings loadSettings(const std::string& path);
bool saveSettings(const std::string& path, const Settings& settings);

// Trimmed; empty terms are ignored; a term already listed moves to the front; at most 8 are kept.
void addRecentSearch(Settings& settings, std::string term);

// The palette entry with this id, else the default (PlayStation blue).
const ui::AccentChoice& accentById(const std::string& id);

// The UI languages (tag of resources/i18n, name in that language), English first.
const std::vector<std::pair<std::string, std::string>>& uiLanguages();

// Auto (40 Mbit/s), 20, 10, 4 and 1.5 Mbit/s.
const std::vector<std::int64_t>& bitrateChoices();

}  // namespace playfin::app
