#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "net/url.hpp"

using playfin::net::candidateUrls;
using playfin::net::parseUrl;
using Strings = std::vector<std::string>;

TEST_CASE("parseUrl normalises scheme, host, port and path") {
    const auto url = parseUrl("  HTTPS://Media.Example.com:8920/Jellyfin/  ");
    REQUIRE(url.has_value());
    CHECK(url->scheme == "https");
    CHECK(url->host == "media.example.com");
    CHECK(url->port == 8920);
    CHECK(url->path == "/Jellyfin");
    CHECK(playfin::net::toString(*url) == "https://media.example.com:8920/Jellyfin");
    CHECK(playfin::net::origin(*url) == "https://media.example.com:8920");
    const auto v6 = parseUrl("http://[::1]:8096");
    REQUIRE(v6.has_value());
    CHECK(v6->host == "[::1]");
    CHECK(v6->port == 8096);
}

TEST_CASE("parseUrl rejects what is not a plain http(s) server address") {
    for (const char* bad : {"", "ftp://host", "http://", "http://user:pw@host", "http://host:0", "http://host:70000",
                            "http://host:abc", "http://ho st", "http://host/?q=1", "http://host#x", "http://[::1",
                            "http://host:", "javascript:alert(1)", "http://ho\\st", "http://[zz]"}) {
        INFO(bad);
        CHECK_FALSE(parseUrl(bad).has_value());
    }
}

TEST_CASE("candidateUrls with a scheme tries that address, plus the Jellyfin port when none is given") {
    CHECK(candidateUrls("https://media.example.com/jellyfin") ==
          Strings{"https://media.example.com/jellyfin", "https://media.example.com:8920/jellyfin"});
    CHECK(candidateUrls("http://192.168.1.10:8096") == Strings{"http://192.168.1.10:8096"});
}

TEST_CASE("candidateUrls without a scheme tries https first, then http") {
    CHECK(candidateUrls("192.168.1.10") == Strings{"https://192.168.1.10", "https://192.168.1.10:8920",
                                                   "http://192.168.1.10", "http://192.168.1.10:8096"});
    CHECK(candidateUrls("NAS.local:8096/") == Strings{"https://nas.local:8096", "http://nas.local:8096"});
}

TEST_CASE("candidateUrls rejects hostile input") {
    for (const char* bad : {"", "   ", "user@host", "host name", "ftp://x", "http://x:99999", "a\nb", "x?y"}) {
        INFO(bad);
        CHECK(candidateUrls(bad).empty());
    }
}

TEST_CASE("urlEncode and Query") {
    CHECK(playfin::net::urlEncode("a b&c=d/\xC3\xA9~_.-") == "a%20b%26c%3Dd%2F%C3%A9~_.-");
    CHECK(playfin::net::Query().add("userId", "u 1").add("limit", 20).str() == "?userId=u%201&limit=20");
    CHECK(playfin::net::Query().str().empty());
}
