#include <doctest/doctest.h>

#include <string>

#include "jellyfin/client.hpp"
#include "net/crypto.hpp"
#include "support/fake_jellyfin.hpp"

using namespace playfin;
using core::ErrorKind;
using jellyfin::Client;
using jellyfin::ImageType;
using jellyfin::ItemQuery;
using jellyfin::Session;
using test::FakeJellyfin;
using test::TlsMode;

namespace {

net::HttpClient http() {
    net::HttpConfig config;
    config.caFile = test::fixturePath("tls/ca.crt");
    return net::HttpClient(config);
}

Session session(const std::string& baseUrl) {
    Session s;
    s.baseUrl = baseUrl;
    s.deviceId = "dev-1";
    s.deviceName = "Linux";
    return s;
}

}  // namespace

TEST_CASE("authorization header") {
    Session s = session("http://x");
    s.deviceName = "PlayStation 4";
    s.deviceId = "d1";
    const std::string base =
        "MediaBrowser Client=\"Playfin\", Device=\"PlayStation 4\", DeviceId=\"d1\", Version=\"0.1.0\"";
    CHECK(jellyfin::authorizationHeader(s) == base);
    s.token = "t\"x,y\n";
    CHECK(jellyfin::authorizationHeader(s) == base + ", Token=\"txy\"");
}

TEST_CASE("connect finds the server and returns the working address") {
    FakeJellyfin server;
    const auto client = http();
    const auto connection = jellyfin::connect(client, server.baseUrl(), "");
    REQUIRE(connection.ok());
    CHECK(connection.value().baseUrl == server.baseUrl());
    CHECK(connection.value().info.name == "Test Server");
}

TEST_CASE("connect without a scheme falls back to http") {
    FakeJellyfin server;
    const auto client = http();
    const std::string hostPort = server.baseUrl().substr(std::string("http://").size());
    const auto connection = jellyfin::connect(client, hostPort, "");
    REQUIRE(connection.ok());
    CHECK(connection.value().baseUrl == server.baseUrl());
}

TEST_CASE("connect refuses servers older than 10.10") {
    FakeJellyfin server(TlsMode::None, "10.9.11");
    const auto client = http();
    const auto connection = jellyfin::connect(client, server.baseUrl(), "");
    REQUIRE_FALSE(connection.ok());
    CHECK(connection.error().kind == ErrorKind::UnsupportedServer);
    CHECK(connection.error().message.find("10.9.11") != std::string::npos);
}

TEST_CASE("a self-signed server asks for trust, then works once pinned") {
    FakeJellyfin server(TlsMode::SelfSigned);
    const auto client = http();
    const auto first = jellyfin::connect(client, server.baseUrl(), "");
    REQUIRE_FALSE(first.ok());
    REQUIRE(first.error().kind == ErrorKind::CertificateUntrusted);
    const std::string pin = net::curlPin(first.error().certificate.spkiSha256);
    const auto second = jellyfin::connect(client, server.baseUrl(), pin);
    REQUIRE(second.ok());
    CHECK(second.value().info.id == "f1e2d3c4b5a6");
}

TEST_CASE("an untrusted certificate on one address does not hide Jellyfin on the next one") {
    test::FakeServer router(TlsMode::SelfSigned);
    router.start();
    FakeJellyfin server;
    const auto client = http();
    const auto connection = jellyfin::connectCandidates(client, {router.baseUrl(), server.baseUrl()}, "");
    REQUIRE(connection.ok());
    CHECK(connection.value().baseUrl == server.baseUrl());
    const auto onlyRouter = jellyfin::connectCandidates(client, {router.baseUrl(), "http://127.0.0.1:1"}, "");
    REQUIRE_FALSE(onlyRouter.ok());
    CHECK(onlyRouter.error().kind == ErrorKind::CertificateUntrusted);
}

TEST_CASE("bad addresses and silent hosts") {
    const auto client = http();
    const auto invalid = jellyfin::connect(client, "not an address", "");
    REQUIRE_FALSE(invalid.ok());
    CHECK(invalid.error().kind == ErrorKind::InvalidAddress);
    const auto refused = jellyfin::connect(client, "127.0.0.1:1", "");
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().kind == ErrorKind::Network);
}

TEST_CASE("a web server that is not Jellyfin is reported as such") {
    test::FakeServer other;
    other.http().Get("/System/Info/Public", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("<html>hello</html>", "text/html");
    });
    other.start();
    const auto client = http();
    const auto connection = jellyfin::connect(client, other.baseUrl(), "");
    REQUIRE_FALSE(connection.ok());
    CHECK(connection.error().kind == ErrorKind::BadResponse);
}

TEST_CASE("sign in with a password") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    const auto wrong = client.signIn("demo", "nope");
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().kind == ErrorKind::Unauthorized);
    const auto auth = client.signIn("demo", "secret");
    REQUIRE(auth.ok());
    CHECK(auth.value().token == "tok-demo");
    CHECK(auth.value().user.id == "u-demo");
    CHECK(server.last().authorization.find("Token=") == std::string::npos);
    CHECK(server.last().body.find(R"("Pw":"secret")") != std::string::npos);
}

TEST_CASE("Quick Connect flow") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    REQUIRE(client.quickConnectEnabled().ok());
    CHECK(client.quickConnectEnabled().value());
    const auto started = client.quickConnectStart();
    REQUIRE(started.ok());
    CHECK(started.value().code == "123456");
    const auto waiting = client.quickConnectPoll(started.value().secret);
    REQUIRE(waiting.ok());
    CHECK_FALSE(waiting.value().authenticated);
    CHECK_FALSE(client.quickConnectFinish(started.value().secret).ok());
    server.approveQuickConnect();
    REQUIRE(client.quickConnectPoll(started.value().secret).value().authenticated);
    const auto auth = client.quickConnectFinish(started.value().secret);
    REQUIRE(auth.ok());
    CHECK(auth.value().token == "tok-demo");
}

TEST_CASE("authenticated calls send the token in the header, never in the URL") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    const auto libraries = client.libraries();
    REQUIRE(libraries.ok());
    CHECK(libraries.value().items.size() == 2);
    const auto seen = server.last();
    CHECK(seen.authorization.find("Token=\"tok-demo\"") != std::string::npos);
    CHECK(seen.target.find("tok-demo") == std::string::npos);
    CHECK(seen.target.find("userId=u-demo") != std::string::npos);
    REQUIRE(client.currentUser().ok());
    CHECK(client.currentUser().value().name == "demo");
}

TEST_CASE("a refused token is Unauthorized") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("bad", "u-demo");
    const auto libraries = client.libraries();
    REQUIRE_FALSE(libraries.ok());
    CHECK(libraries.error().kind == ErrorKind::Unauthorized);
    CHECK(libraries.error().httpStatus == 401);
}

TEST_CASE("browsing endpoints") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    const auto latest = client.latest("lib-movies", 16);
    REQUIRE(latest.ok());
    CHECK(latest.value().items.size() == 2);
    CHECK(server.last().target.find("parentId=lib-movies") != std::string::npos);
    CHECK(server.last().target.find("limit=16") != std::string::npos);
    CHECK(client.resume(12).value().items.size() == 1);
    CHECK(server.last().target.find("mediaTypes=Video") != std::string::npos);
    CHECK(client.nextUp(12).value().items.size() == 1);
    const auto movie = client.item("m-1");
    REQUIRE(movie.ok());
    CHECK(movie.value().userData.favorite);
    const auto missing = client.item("missing");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == ErrorKind::NotFound);
    CHECK(client.seasons("s-1").value().items.size() == 2);
    CHECK(client.episodes("s-1", "se-1").value().items.size() == 2);
    CHECK(server.last().target.find("seasonId=se-1") != std::string::npos);
}

TEST_CASE("library items are paged and searchable") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    ItemQuery query;
    query.parentId = "lib-movies";
    query.includeTypes = {"Movie"};
    query.startIndex = 200;
    query.limit = 100;
    const auto page = client.items(query);
    REQUIRE(page.ok());
    CHECK(page.value().items.size() == 50);
    CHECK(page.value().total == FakeJellyfin::kMovieCount);
    CHECK(page.value().start == 200);
    const std::string target = server.last().target;
    CHECK(target.find("includeItemTypes=Movie") != std::string::npos);
    CHECK(target.find("sortBy=SortName") != std::string::npos);
    CHECK(target.find("sortOrder=Ascending") != std::string::npos);
    CHECK(target.find("recursive=true") != std::string::npos);
    ItemQuery search;
    search.searchTerm = "movie 12";
    const auto found = client.items(search);
    REQUIRE(found.ok());
    CHECK(found.value().total == 10);  // Movie 120 to 129
    CHECK(server.last().target.find("searchTerm=movie%2012") != std::string::npos);
}

TEST_CASE("favorite and played use POST to set and DELETE to clear") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    REQUIRE(client.setFavorite("m-1", true).ok());
    CHECK(server.last().method == "POST");
    CHECK(server.last().path == "/UserFavoriteItems/m-1");
    REQUIRE(client.setFavorite("m-1", false).ok());
    CHECK(server.last().method == "DELETE");
    REQUIRE(client.setPlayed("m-1", true).ok());
    CHECK(server.last().path == "/UserPlayedItems/m-1");
    REQUIRE(client.signOut().ok());
    CHECK(server.last().path == "/Sessions/Logout");
}

TEST_CASE("image URLs carry no token and fetch sends it only to the same server") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    const std::string url = client.imageUrl("m-1", ImageType::Primary, "t-m1", 300, 450);
    CHECK(url == server.baseUrl() + "/Items/m-1/Images/Primary?tag=t-m1&maxWidth=300&maxHeight=450&format=Jpg&quality=90");
    const auto bytes = client.fetch(url);
    REQUIRE(bytes.ok());
    REQUIRE(bytes.value().size() > 2);
    CHECK(static_cast<unsigned char>(bytes.value()[0]) == 0xFF);
    CHECK(static_cast<unsigned char>(bytes.value()[1]) == 0xD8);
    const auto foreign = client.fetch("http://evil.example/steal");
    REQUIRE_FALSE(foreign.ok());
    CHECK(foreign.error().kind == ErrorKind::BadResponse);
    const auto lookalike = client.fetch(server.baseUrl() + ".evil.example/x");
    CHECK_FALSE(lookalike.ok());
}

TEST_CASE("API replies are limited to 8 MiB, fetched files are not") {
    const std::string huge(jellyfin::kMaxApiReplyBytes + 1, ' ');
    test::FakeServer server;
    server.http().Get("/Users/Me", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(huge + test::readFile(test::fixturePath("jellyfin/user_me.json")), "application/json");
    });
    server.http().Get("/Items/m-1/Images/Primary", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(huge, "image/jpeg");
    });
    server.start();
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("t", "u");
    const auto me = client.currentUser();
    REQUIRE_FALSE(me.ok());
    CHECK(me.error().kind == ErrorKind::BadResponse);
    CHECK(me.error().message.find("too large") != std::string::npos);
    const auto image = client.fetch(server.baseUrl() + "/Items/m-1/Images/Primary");
    REQUIRE(image.ok());
    CHECK(image.value().size() == huge.size());
}

TEST_CASE("server errors and broken replies") {
    test::FakeServer broken;
    broken.http().Get("/UserViews", [](const httplib::Request&, httplib::Response& res) { res.status = 500; });
    broken.http().Get("/Users/Me", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"Name":"trunc)", "application/json");
    });
    broken.start();
    const auto httpClient = http();
    Client client(httpClient, session(broken.baseUrl()));
    client.setToken("t", "u");
    const auto views = client.libraries();
    REQUIRE_FALSE(views.ok());
    CHECK(views.error().kind == ErrorKind::Server);
    CHECK(views.error().httpStatus == 500);
    const auto me = client.currentUser();
    REQUIRE_FALSE(me.ok());
    CHECK(me.error().kind == ErrorKind::BadResponse);
}

TEST_CASE("user images have their own URL, without the token, and can be fetched") {
    FakeJellyfin server;
    const auto httpClient = http();
    Client client(httpClient, session(server.baseUrl()));
    const std::string url = client.userImageUrl("u-demo", "img-demo", 180);
    CHECK(url == server.baseUrl() + "/Users/u-demo/Images/Primary?tag=img-demo&maxWidth=180&maxHeight=180&format=Jpg&quality=90");
    const auto bytes = client.fetch(url);
    REQUIRE(bytes.ok());
    CHECK(static_cast<unsigned char>(bytes.value()[0]) == 0xFF);
}

namespace {
Client signedIn(const net::HttpClient& httpClient, const FakeJellyfin& server) {
    Client client(httpClient, session(server.baseUrl()));
    client.setToken("tok-demo", "u-demo");
    return client;
}
}  // namespace

TEST_CASE("music: album artists, an artist's albums and a playlist's tracks") {
    FakeJellyfin server;
    const auto httpClient = http();
    const Client client = signedIn(httpClient, server);
    const auto artists = client.artists("lib-music", 0, 100);
    REQUIRE(artists.ok());
    CHECK(artists.value().items.size() == 3);
    CHECK(server.last().path == "/Artists/AlbumArtists");
    CHECK(server.last().target.find("parentId=lib-music") != std::string::npos);
    ItemQuery query;
    query.albumArtistId = "6233866b316f71faa355d1f0d999f963";
    query.includeTypes = {"MusicAlbum"};
    REQUIRE(client.items(query).ok());
    CHECK(server.last().target.find("albumArtistIds=6233866b316f71faa355d1f0d999f963") != std::string::npos);
    const auto tracks = client.playlistItems("pl-1");
    REQUIRE(tracks.ok());
    CHECK(tracks.value().items.size() == 8);
    CHECK(server.last().path == "/Playlists/pl-1/Items");
}

TEST_CASE("Live TV: channels, programmes, what is on, recordings") {
    FakeJellyfin server;
    const auto httpClient = http();
    const Client client = signedIn(httpClient, server);
    const auto channels = client.liveTvChannels();
    REQUIRE(channels.ok());
    CHECK(channels.value().items.size() == 3);
    CHECK(server.last().target.find("addCurrentProgram=true") != std::string::npos);
    const auto programs = client.programs("ch-1", 1792179000, 1792265400);
    REQUIRE(programs.ok());
    CHECK(server.last().path == "/LiveTv/Programs");
    CHECK(server.last().target.find("channelIds=ch-1") != std::string::npos);
    CHECK(server.last().target.find("minEndDate=2026-10-16T19%3A30%3A00Z") != std::string::npos);
    CHECK(server.last().target.find("maxStartDate=2026-10-17T19%3A30%3A00Z") != std::string::npos);
    CHECK(server.last().target.find("sortBy=StartDate") != std::string::npos);
    REQUIRE(client.airingNow(16).ok());
    CHECK(server.last().path == "/LiveTv/Programs/Recommended");
    CHECK(server.last().target.find("isAiring=true") != std::string::npos);
    REQUIRE(client.recordings().ok());
    CHECK(server.last().path == "/LiveTv/Recordings");
}

TEST_CASE("Live TV: a recording is scheduled from the server's defaults and cancelled") {
    FakeJellyfin server;
    const auto httpClient = http();
    const Client client = signedIn(httpClient, server);
    REQUIRE(client.createTimer("prog-7").ok());
    const auto requests = server.requests();
    REQUIRE(requests.size() >= 2);
    CHECK(requests[requests.size() - 2].target.find("/LiveTv/Timers/Defaults?programId=prog-7") == 0);
    CHECK(requests.back().method == "POST");
    CHECK(requests.back().path == "/LiveTv/Timers");
    CHECK(server.timerBody().find(R"("ProgramId":"1bbabe69245f97e2485d7053b72ae3a9")") != std::string::npos);
    const auto timers = client.timers();
    REQUIRE(timers.ok());
    CHECK(timers.value().size() == 1);
    REQUIRE(client.cancelTimer("t-1").ok());
    CHECK(server.last().method == "DELETE");
    CHECK(server.last().path == "/LiveTv/Timers/t-1");
}

TEST_CASE("Live TV: a channel's live stream is opened by PlaybackInfo and closed") {
    FakeJellyfin server;
    const auto httpClient = http();
    const Client client = signedIn(httpClient, server);
    jellyfin::PlaybackRequest request;
    request.liveTv = true;
    const auto info = client.playbackInfo("79cf8fe44f3f38fbe05265ad38d3f2aa", request);
    REQUIRE(info.ok());
    CHECK_FALSE(info.value().sources.front().liveStreamId.empty());
    const std::string body = server.playbackInfoBody();
    CHECK(body.find(R"("AutoOpenLiveStream":true)") != std::string::npos);
    CHECK(body.find(R"("EnableDirectPlay":false)") != std::string::npos);
    REQUIRE(client.closeLiveStream("ls-1").ok());
    CHECK(server.last().method == "POST");
    CHECK(server.last().target == "/LiveStreams/Close?liveStreamId=ls-1");
}
