#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/fixed_metrics.hpp"

using playfin::test::FixedMetrics;
using playfin::ui::FontStyle;
using Lines = std::vector<std::string>;

// size 10: every code point is 5 units wide
const FontStyle kStyle{10, playfin::ui::FontWeight::Regular};

TEST_CASE("short text stays on one line") {
    FixedMetrics metrics;
    CHECK(metrics.breakLines("hello world", kStyle, 100) == Lines{"hello world"});
}

TEST_CASE("words wrap at the width") {
    FixedMetrics metrics;
    // 6 characters per line (30 units)
    CHECK(metrics.breakLines("aa bb cc dd ee", kStyle, 30) == Lines{"aa bb", "cc dd", "ee"});
}

TEST_CASE("explicit newlines break lines, empty lines are kept") {
    FixedMetrics metrics;
    CHECK(metrics.breakLines("one\n\ntwo", kStyle, 100) == Lines{"one", "", "two"});
}

TEST_CASE("long words and CJK text break between characters") {
    FixedMetrics metrics;
    CHECK(metrics.breakLines("abcdefgh", kStyle, 15) == Lines{"abc", "def", "gh"});
    CHECK(metrics.breakLines("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", kStyle, 10) ==
          Lines{"\xE6\x97\xA5\xE6\x9C\xAC", "\xE8\xAA\x9E"});
}

TEST_CASE("maxLines cuts with an ellipsis that fits") {
    FixedMetrics metrics;
    const Lines lines = metrics.breakLines("aa bb cc dd ee", kStyle, 30, 2);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "aa bb");
    CHECK(lines[1] == "cc dd\xE2\x80\xA6");
    CHECK(metrics.measure(lines[1], kStyle) <= 30);
}

TEST_CASE("text that never fits still terminates") {
    FixedMetrics metrics;
    const std::string word(500, 'x');
    const Lines one = metrics.breakLines(word, kStyle, 10, 1);
    REQUIRE(one.size() == 1);
    CHECK(one[0] == "x\xE2\x80\xA6");
    const Lines tiny = metrics.breakLines(word, kStyle, 1, 1);
    REQUIRE(tiny.size() == 1);
    CHECK(tiny[0] == "\xE2\x80\xA6");
    CHECK(metrics.breakLines("abc", kStyle, 0) == Lines{"abc"});
    CHECK(metrics.breakLines("", kStyle, 100) == Lines{""});
}
