#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace playfin::ui {

// "one", "few", "many" or "other" for a count in a language (en/fr/de/es/pt/tr/vi/ja/ko/zh, ru/uk, cs).
const char* pluralCategory(std::string_view language, long long n);

// String tables: resources/i18n/<tag>.json, flat {"key": "text"} with {0}, {1}… placeholders and plurals as
// "key.one" / "key.few" / "key.many" / "key.other". Lookup: local strings, English, then the key itself.
class Translator {
public:
    // Loads English plus the best match of `language` (exact tag, then the part before '-').
    bool load(const std::string& dir, const std::string& language);
    void loadStrings(std::string_view englishJson, std::string_view localJson, const std::string& language);

    const std::string& language() const { return language_; }  // "" when only English is loaded
    std::string tr(std::string_view key, std::initializer_list<std::string_view> args = {}) const;
    // {0} is the count; further args are {1}, {2}…
    std::string plural(std::string_view key, long long count, std::initializer_list<std::string_view> args = {}) const;
    std::vector<std::string> missingKeys() const;

private:
    const std::string* find(std::string_view key) const;

    std::map<std::string, std::string, std::less<>> english_;
    std::map<std::string, std::string, std::less<>> local_;
    std::string language_;
};

Translator& translator();
std::string tr(std::string_view key, std::initializer_list<std::string_view> args = {});

}  // namespace playfin::ui
