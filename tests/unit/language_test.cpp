#include <doctest/doctest.h>

#include "platform/language.hpp"

using playfin::platform::languageFromPosix;
using playfin::platform::languageFromPs4Id;

TEST_CASE("PS4 system language ids map to BCP-47 tags") {
    CHECK(languageFromPs4Id(0) == "ja-JP");
    CHECK(languageFromPs4Id(1) == "en-US");
    CHECK(languageFromPs4Id(2) == "fr-FR");
    CHECK(languageFromPs4Id(11) == "zh-Hans");
    CHECK(languageFromPs4Id(17) == "pt-BR");
    CHECK(languageFromPs4Id(-1) == "en-US");
    CHECK(languageFromPs4Id(999) == "en-US");
}

TEST_CASE("POSIX locales map to BCP-47 tags") {
    CHECK(languageFromPosix("fr_FR.UTF-8") == "fr-FR");
    CHECK(languageFromPosix("de_DE@euro") == "de-DE");
    CHECK(languageFromPosix("pt") == "pt");
    CHECK(languageFromPosix("C") == "en-US");
    CHECK(languageFromPosix("POSIX") == "en-US");
    CHECK(languageFromPosix("C.UTF-8") == "en-US");
    CHECK(languageFromPosix("") == "en-US");
}
