#include "platform/language.hpp"

namespace playfin::platform {

namespace {

// Index = PS4 language id
constexpr const char* kPs4Languages[] = {
    "ja-JP",   "en-US",   "fr-FR", "es-ES", "de-DE", "it-IT",  "nl-NL", "pt-PT", "ru-RU", "ko-KR", "zh-Hant",
    "zh-Hans", "fi-FI",   "sv-SE", "da-DK", "nb-NO", "pl-PL",  "pt-BR", "en-GB", "tr-TR", "es-419", "ar",
    "fr-CA",   "cs-CZ",   "hu-HU", "el-GR", "ro-RO", "th-TH",  "vi-VN", "id-ID", "uk-UA",
};
constexpr int kPs4LanguageCount = static_cast<int>(sizeof(kPs4Languages) / sizeof(kPs4Languages[0]));

}  // namespace

std::string languageFromPs4Id(int id) {
    if (id < 0 || id >= kPs4LanguageCount) {
        return "en-US";
    }
    return kPs4Languages[id];
}

std::string languageFromPosix(std::string_view locale) {
    const std::size_t end = locale.find_first_of(".@");
    std::string tag(locale.substr(0, end));
    if (tag.empty() || tag == "C" || tag == "POSIX") {
        return "en-US";
    }
    for (char& c : tag) {
        if (c == '_') {
            c = '-';
        }
    }
    return tag;
}

}  // namespace playfin::platform
