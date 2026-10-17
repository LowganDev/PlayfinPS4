#include <doctest/doctest.h>

#include <string>

#include "support/files.hpp"
#include "ui/i18n.hpp"

using playfin::ui::Translator;

namespace {

const char* kEnglish = R"({"hello":"Hello","greet":"Hello {0}, you have {1}","items.one":"{0} item",)"
                       R"("items.other":"{0} items","only_en":"English only"})";
const char* kFrench = R"({"hello":"Bonjour","items.one":"{0} élément","items.other":"{0} éléments"})";

}  // namespace

TEST_CASE("translations fall back to English, then to the key") {
    Translator t;
    t.loadStrings(kEnglish, kFrench, "fr");
    CHECK(t.tr("hello") == "Bonjour");
    CHECK(t.tr("only_en") == "English only");
    CHECK(t.tr("missing.key") == "missing.key");
}

TEST_CASE("placeholders") {
    Translator t;
    t.loadStrings(kEnglish, "{}", "en");
    CHECK(t.tr("greet", {"Ana", "mail"}) == "Hello Ana, you have mail");
    CHECK(t.tr("greet", {"Ana"}) == "Hello Ana, you have {1}");
}

TEST_CASE("plurals follow the language rules") {
    Translator t;
    t.loadStrings(kEnglish, kFrench, "fr");
    CHECK(t.plural("items", 0) == "0 élément");
    CHECK(t.plural("items", 1) == "1 élément");
    CHECK(t.plural("items", 2) == "2 éléments");
    Translator en;
    en.loadStrings(kEnglish, "{}", "en");
    CHECK(en.plural("items", 0) == "0 items");
    CHECK(en.plural("items", 1) == "1 item");
    CHECK(std::string(playfin::ui::pluralCategory("ru", 21)) == "one");
    CHECK(std::string(playfin::ui::pluralCategory("ru", 22)) == "few");
    CHECK(std::string(playfin::ui::pluralCategory("ru", 25)) == "many");
    CHECK(std::string(playfin::ui::pluralCategory("ru", 11)) == "many");
    CHECK(std::string(playfin::ui::pluralCategory("cs", 3)) == "few");
    CHECK(std::string(playfin::ui::pluralCategory("ja", 1)) == "other");
}

TEST_CASE("broken translation files are ignored") {
    Translator t;
    t.loadStrings(kEnglish, "{not json", "fr");
    CHECK(t.tr("hello") == "Hello");
}

TEST_CASE("the shipped files resolve exact tag, then language, then English") {
    const std::string dir = std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n";
    Translator fr;
    REQUIRE(fr.load(dir, "fr-FR"));
    CHECK(fr.language() == "fr");
    CHECK(fr.tr("ui.cancel") == "Annuler");
    Translator zh;
    REQUIRE(zh.load(dir, "zh-Hant"));
    CHECK(zh.language() == "zh-Hant");
    Translator unknown;
    REQUIRE(unknown.load(dir, "xx-YY"));
    CHECK(unknown.language().empty());
    CHECK(unknown.tr("ui.cancel") == "Cancel");
}

TEST_CASE("every shipped language has every English key") {
    const std::string dir = std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n";
    for (const char* language : {"fr", "de", "es", "pt", "ru", "uk", "cs", "tr", "vi", "ja", "ko", "zh-Hans", "zh-Hant"}) {
        INFO(language);
        Translator t;
        REQUIRE(t.load(dir, language));
        CHECK(t.language() == language);
        CHECK(t.missingKeys().empty());
    }
}
