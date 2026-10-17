#include <doctest/doctest.h>

#include "core/strings.hpp"

using namespace playfin;

TEST_CASE("asciiLower lowers ASCII letters and keeps every other byte") {
    CHECK(core::asciiLower("HTTPS://Media.Example") == "https://media.example");
    CHECK(core::asciiLower("H264 \xC3\x89t\xC3\xA9") == "h264 \xC3\x89t\xC3\xA9");
}

TEST_CASE("trimmed drops spaces, tabs and line ends at both ends") {
    CHECK(core::trimmed("  \tCN=Example\r\n") == "CN=Example");
    CHECK(core::trimmed(" a b ") == "a b");
    CHECK(core::trimmed(" \r\n").empty());
}
