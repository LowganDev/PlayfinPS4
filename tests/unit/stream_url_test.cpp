#include <doctest/doctest.h>

#include <string>

#include "player/stream_url.hpp"

using namespace playfin;

TEST_CASE("the direct stream URL names the item, source and session, never the token") {
    jellyfin::Session session;
    session.baseUrl = "https://h:1";
    session.token = "tok-secret";
    CHECK(player::directStreamUrl(session, "i1", "ms1", "ps1") ==
          "https://h:1/Videos/i1/stream?static=true&mediaSourceId=ms1&playSessionId=ps1");
    CHECK(player::directStreamUrl(session, "i1", "", "") == "https://h:1/Videos/i1/stream?static=true");
    CHECK(player::directStreamUrl(session, "i1", "ms1", "ps1").find("tok-secret") == std::string::npos);
}

TEST_CASE("api keys are removed from server URLs, the rest is kept in order") {
    const std::string url = "https://h/x.m3u8?a=1&api_key=SECRET&ApiKey=S2&b=2";
    CHECK(player::withoutApiKey(url) == "https://h/x.m3u8?a=1&b=2");
    CHECK(player::withoutApiKey("https://h/x?API_KEY=Z") == "https://h/x");
    CHECK(player::withoutApiKey("https://h/x?api_key=Z&c=3") == "https://h/x?c=3");
    CHECK(player::withoutApiKey("https://h/x") == "https://h/x");
    CHECK(player::withoutApiKey("https://h/x?keep=api_key") == "https://h/x?keep=api_key");
}

TEST_CASE("music streams from the audio endpoint") {
    jellyfin::Session session;
    session.baseUrl = "https://h:1";
    CHECK(player::directStreamUrl(session, "a1", "a1", "ps1", player::MediaKind::Audio) ==
          "https://h:1/Audio/a1/stream?static=true&mediaSourceId=a1&playSessionId=ps1");
}
