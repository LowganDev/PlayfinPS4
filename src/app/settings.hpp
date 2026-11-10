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
    int volume = 100;                     // the player's, 0 to 100
    std::string pictureFit = "fit";       // "fit", "fill" or "stretch"
    int pictureSize = 100;                // percent of the screen the picture may take, 80 to 100
    std::vector<std::string> recentChannels;  // Live TV channel ids, last watched first, at most 8
    std::string visualizer = "bars";  // the music visualizer: "bars", "mirror", "wave" or "off"
    std::string downloadTarget = "console";  // where downloads go: "console" or "usb" (the first USB drive)
    std::string playMethod = "auto";  // videos: "auto", "direct" (never a transcode) or "transcode" (always one)
    std::string xmltvUrl;  // a TV guide read on the console (http or https), "" = the server's only
    std::vector<std::string> libraryOrder;  // library ids in the order the user chose ("Mes médias", latest rows)
};

// Missing file: defaults. Damaged file: renamed .bad, defaults. Unknown or invalid values: their defaults.
Settings loadSettings(const std::string& path);
bool saveSettings(const std::string& path, const Settings& settings);

// Trimmed; empty terms are ignored; a term already listed moves to the front; at most 8 are kept.
void addRecentSearch(Settings& settings, std::string term);

// An XMLTV address the settings keep: http or https, no spaces, at most 2048 bytes.
bool validGuideUrl(const std::string& url);

// The palette entry with this id, else the default (PlayStation blue).
const ui::AccentChoice& accentById(const std::string& id);

// The UI languages (tag of resources/i18n, name in that language), English first.
const std::vector<std::pair<std::string, std::string>>& uiLanguages();

// Auto (40 Mbit/s), 20, 10, 4 and 1.5 Mbit/s.
const std::vector<std::int64_t>& bitrateChoices();

}  // namespace playfin::app
