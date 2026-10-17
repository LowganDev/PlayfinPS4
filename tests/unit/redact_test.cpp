#include <doctest/doctest.h>

#include <string>

#include "core/redact.hpp"

using playfin::core::redact;

TEST_CASE("redact masks API keys in query strings, any case") {
    CHECK(redact("GET /Items?api_key=abc123&Limit=5") == "GET /Items?api_key=***&Limit=5");
    CHECK(redact("url ApiKey=zz end") == "url ApiKey=*** end");
    CHECK(redact("API_KEY=XYZ") == "API_KEY=***");
    CHECK(redact("login password=p4ss&x=1") == "login password=***&x=1");
}

TEST_CASE("redact masks authentication headers up to the end of the line") {
    CHECK(redact("X-Emby-Token: secret\nnext") == "X-Emby-Token: ***\nnext");
    CHECK(redact("Authorization: MediaBrowser Client=\"Playfin\", Token=\"t0k\"") == "Authorization: ***");
    CHECK(redact("x-mediabrowser-token:abc") == "x-mediabrowser-token:***");
}

TEST_CASE("redact masks JSON passwords and tokens") {
    CHECK(redact(R"({"Username":"bob","Pw":"hunter2"})") == R"({"Username":"bob","Pw":"***"})");
    CHECK(redact(R"({"AccessToken": "a\"b", "x":1})") == R"({"AccessToken": "***", "x":1})");
    CHECK(redact(R"({"Password":null})") == R"({"Password":null})");
}

TEST_CASE("redact masks every occurrence and leaves clean text alone") {
    CHECK(redact("api_key=a api_key=b") == "api_key=*** api_key=***");
    CHECK(redact("nothing secret here") == "nothing secret here");
    CHECK(redact("") == "");
    CHECK(redact("api_key=") == "api_key=");
}

TEST_CASE("redact handles truncated JSON strings") {
    CHECK(redact(R"("Pw":"abc)") == R"("Pw":"***)");
    CHECK(redact("\"Pw\":\"ab\\") == "\"Pw\":\"***");
}

TEST_CASE("redact masks password change bodies") {
    CHECK(redact(R"({"CurrentPw":"old","NewPw":"new"})") == R"({"CurrentPw":"***","NewPw":"***"})");
}

TEST_CASE("redact masks a token inside any MediaBrowser header value") {
    CHECK(redact(R"(MediaBrowser Client="Playfin", Token="abc123")") == R"(MediaBrowser Client="Playfin", Token="***")");
}

TEST_CASE("redact masks the Quick Connect secret") {
    CHECK(redact("GET http://h/QuickConnect/Connect?secret=9f8e7d&x=1 failed") ==
          "GET http://h/QuickConnect/Connect?secret=***&x=1 failed");
    CHECK(redact(R"({"Secret":"9f8e7d","Code":"123456"})") == R"({"Secret":"***","Code":"123456"})");
}

TEST_CASE("redact allows spaces around the JSON colon") {
    CHECK(redact(R"({"AccessToken" : "abc"})") == R"({"AccessToken" : "***"})");
}

TEST_CASE("redact masks long lines full of secrets") {
    std::string line;
    for (int i = 0; i < 2000; ++i) {
        line += "api_key=x ";
    }
    const std::string masked = redact(line);
    CHECK(masked.find("x ") == std::string::npos);
    CHECK(masked.find("api_key=***") == 0);
}
