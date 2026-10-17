#include <doctest/doctest.h>

#include <string>

#include "core/utf.hpp"

using playfin::core::utf16ToUtf8;
using playfin::core::utf8ToUtf16;

TEST_CASE("UTF-8 to UTF-16 and back") {
    CHECK(utf8ToUtf16("abc") == u"abc");
    CHECK(utf8ToUtf16("\xC3\xA9") == u"é");
    CHECK(utf8ToUtf16("\xE6\x97\xA5\xE6\x9C\xAC") == u"日本");
    CHECK(utf8ToUtf16("\xF0\x9F\x98\x80") == u"\U0001F600");
    for (const char* text : {"", "Playfin", "\xC3\xA9t\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "\xF0\x9F\x98\x80!"}) {
        CHECK(utf16ToUtf8(utf8ToUtf16(text)) == text);
    }
}

TEST_CASE("invalid sequences become U+FFFD") {
    CHECK(utf8ToUtf16("a\xFF" "b") == u"a�b");
    CHECK(utf8ToUtf16("\xC3") == u"�");
    CHECK(utf8ToUtf16("\xC0\xAF") == u"��");      // overlong
    CHECK(utf8ToUtf16("\xED\xA0\x80") == u"���");  // encoded surrogate
    std::u16string lone;
    lone += static_cast<char16_t>(0xD800);
    lone += u'x';
    CHECK(utf16ToUtf8(lone) == "\xEF\xBF\xBDx");
}

TEST_CASE("code point stepping") {
    const std::string text = "a\xC3\xA9\xE6\x97\xA5";
    std::size_t pos = 0;
    CHECK(playfin::core::nextCodePoint(text, pos) == U'a');
    CHECK(playfin::core::nextCodePoint(text, pos) == U'é');
    CHECK(playfin::core::nextCodePoint(text, pos) == U'日');
    CHECK(pos == text.size());
    CHECK(playfin::core::previousCodePointStart(text, text.size()) == 3);
    CHECK(playfin::core::previousCodePointStart(text, 3) == 1);
    CHECK(playfin::core::previousCodePointStart(text, 0) == 0);
}
