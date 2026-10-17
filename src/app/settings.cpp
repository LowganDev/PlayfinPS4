#include "app/settings.hpp"

#include <algorithm>
#include <cstdio>

#include "core/file_io.hpp"
#include "core/log.hpp"
#include "core/strings.hpp"
#include "jellyfin/json.hpp"

namespace playfin::app {

namespace {

constexpr std::size_t kMaxFileBytes = 64 * 1024;
constexpr std::size_t kMaxRecent = 8;
constexpr std::size_t kMaxTermBytes = 200;

bool knownAccent(const std::string& id) {
    const auto& palette = ui::accentPalette();
    return std::any_of(palette.begin(), palette.end(), [&](const ui::AccentChoice& c) { return id == c.id; });
}

}  // namespace

Settings loadSettings(const std::string& path) {
    Settings settings;
    std::string text;
    switch (core::readSmallFile(path, kMaxFileBytes, text)) {
    case core::ReadStatus::Missing:
        return settings;
    case core::ReadStatus::Failed:
        PF_LOG_WARN("cannot read %s: default settings", path.c_str());
        return settings;
    case core::ReadStatus::Ok:
        break;
    }
    const auto doc = jellyfin::json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root())) {
        PF_LOG_WARN("%s is damaged: kept as .bad, default settings", path.c_str());
        std::rename(path.c_str(), (path + ".bad").c_str());
        return settings;
    }
    yyjson_val* root = doc->root();
    const std::string accent = jellyfin::json::string(root, "accent");
    if (knownAccent(accent)) {
        settings.accent = accent;
    }
    const std::int64_t bitrate = jellyfin::json::integer(root, "maxBitrate", 0);
    if (bitrate > 0) {
        settings.maxBitrate = bitrate;
    }
    settings.language = jellyfin::json::string(root, "language");
    for (const std::string& term : jellyfin::json::strings(root, "recentSearches")) {
        if (settings.recentSearches.size() < kMaxRecent && !core::trimmed(term).empty()) {
            settings.recentSearches.push_back(term.substr(0, kMaxTermBytes));
        }
    }
    return settings;
}

bool saveSettings(const std::string& path, const Settings& settings) {
    jellyfin::json::Writer writer;
    writer.object();
    writer.key("version").value(1);
    writer.key("accent").value(settings.accent);
    writer.key("maxBitrate").value(settings.maxBitrate);
    writer.key("language").value(settings.language);
    writer.key("recentSearches").array();
    for (const std::string& term : settings.recentSearches) {
        writer.value(term);
    }
    writer.end();
    writer.end();
    return core::writeFileAtomic(path, writer.str());
}

void addRecentSearch(Settings& settings, std::string term) {
    term = std::string(core::trimmed(term)).substr(0, kMaxTermBytes);
    if (term.empty()) {
        return;
    }
    auto& list = settings.recentSearches;
    list.erase(std::remove(list.begin(), list.end(), term), list.end());
    list.insert(list.begin(), std::move(term));
    if (list.size() > kMaxRecent) {
        list.resize(kMaxRecent);
    }
}

const ui::AccentChoice& accentById(const std::string& id) {
    for (const ui::AccentChoice& choice : ui::accentPalette()) {
        if (id == choice.id) {
            return choice;
        }
    }
    return ui::accentPalette().front();
}

const std::vector<std::pair<std::string, std::string>>& uiLanguages() {
    static const std::vector<std::pair<std::string, std::string>> languages = {
        {"en", "English"},
        {"fr", "Fran\xC3\xA7" "ais"},
        {"de", "Deutsch"},
        {"es", "Espa\xC3\xB1ol"},
        {"pt", "Portugu\xC3\xAAs"},
        {"cs", "\xC4\x8C" "e\xC5\xA1tina"},
        {"tr", "T\xC3\xBCrk\xC3\xA7" "e"},
        {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},
        {"uk", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0"},
        {"vi", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t"},
        {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"},
        {"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"},
        {"zh-Hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87"},
        {"zh-Hant", "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87"},
    };
    return languages;
}

const std::vector<std::int64_t>& bitrateChoices() {
    static const std::vector<std::int64_t> choices = {40000000, 20000000, 10000000, 4000000, 1500000};
    return choices;
}

}  // namespace playfin::app
