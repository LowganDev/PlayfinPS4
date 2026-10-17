#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "app/shell/format.hpp"
#include "app/shell/session.hpp"
#include "jellyfin/models.hpp"
#include "support/fake_jellyfin.hpp"
#include "support/files.hpp"
#include "ui/i18n.hpp"

using namespace playfin;
using core::ErrorKind;

namespace {

struct English {
    English() { ui::translator().load(std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n", "en"); }
};

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/playfin-shell-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

jellyfin::Item fixtureItem(const char* name) { return jellyfin::parseItem(test::readFixture(name)).value(); }

net::HttpConfig testHttp() {
    net::HttpConfig config;
    config.caFile = test::fixturePath("tls/ca.crt");
    return config;
}

}  // namespace

TEST_CASE("errors become sentences") {
    English english;
    CHECK(app::errorText(core::makeError(ErrorKind::InvalidAddress, "")) == "This is not a valid server address.");
    CHECK(app::errorText(core::makeError(ErrorKind::Network, "x")).find("cannot be reached") != std::string::npos);
    CHECK(app::errorText(core::makeError(ErrorKind::UnsupportedServer, "Jellyfin 10.9.11")) ==
          "This server runs Jellyfin 10.9.11. Playfin needs Jellyfin 10.10 or newer: please update the server.");
    CHECK(app::errorText(core::makeError(ErrorKind::BadResponse, "not a Jellyfin server")) ==
          "This address does not answer like a Jellyfin server.");
    CHECK(app::errorText(core::makeError(ErrorKind::Unauthorized, "")) == "Wrong user name or password.");
    CHECK(app::errorText(core::makeError(ErrorKind::Server, "HTTP 500")) == "Something went wrong: HTTP 500");
}

TEST_CASE("item texts") {
    English english;
    const auto movie = fixtureItem("jellyfin/item_movie.json");
    CHECK(app::metaLine(movie) == "2008 · 10 min · PG · ★ 6.5");
    CHECK(app::cardTitle(movie) == "Big Buck Bunny");
    CHECK(app::cardSubtitle(movie) == "2008");
    const auto episode = fixtureItem("jellyfin/item_episode.json");
    CHECK(app::cardTitle(episode) == "Test Show");
    CHECK(app::cardSubtitle(episode) == "S1 · E1 Pilot");
    jellyfin::Item program;
    program.type = jellyfin::ItemType::Program;
    program.startUtc = jellyfin::parseUtc("2026-10-16T19:30:00Z");
    program.endUtc = jellyfin::parseUtc("2026-10-16T20:30:00Z");
    CHECK(app::cardSubtitle(program, 120) == "21:30 \xE2\x80\x93 22:30");  // the app's clock (Paris), not the machine's
}

TEST_CASE("image choices: thumbnails for wide cards, series posters for episodes") {
    jellyfin::Session session;
    session.baseUrl = "http://s";
    net::HttpClient http(testHttp());
    jellyfin::Client client(http, session);
    const auto episode = fixtureItem("jellyfin/item_episode.json");
    CHECK(app::imageUrlFor(client, episode, true, 320, 180).find("/Items/e-1/Images/Primary?tag=t-e1") != std::string::npos);
    CHECK(app::imageUrlFor(client, episode, false, 200, 300).find("/Items/s-1/Images/Primary?tag=t-s1") != std::string::npos);
    const auto movie = fixtureItem("jellyfin/item_movie.json");
    CHECK(app::imageUrlFor(client, movie, true, 320, 180).find("/Items/m-1/Images/Thumb?tag=t-m1t") != std::string::npos);
    CHECK(app::imageUrlFor(client, movie, false, 200, 300).find("/Items/m-1/Images/Primary?tag=t-m1") != std::string::npos);
    jellyfin::Item bare;
    bare.id = "x";
    CHECK(app::imageUrlFor(client, bare, false, 200, 300).empty());
}

TEST_CASE("the session signs in, saves the token and starts at home next time") {
    test::FakeJellyfin server;
    TempDir dir;
    const std::string store = dir.path + "/servers.json";
    {
        app::AppSession session(testHttp(), store);
        REQUIRE(session.load().ok());
        CHECK(app::startTarget(session) == app::StartTarget::Servers);
        CHECK_FALSE(session.fetchImage(server.baseUrl() + "/Items/m-1/Images/Primary", {}).ok());
        auto& stored = jellyfin::upsertServer(session.data(), "f1e2d3c4b5a6");
        jellyfin::rememberUrl(stored, server.baseUrl());
        session.useServer("f1e2d3c4b5a6", server.baseUrl());
        const auto auth = session.client()->signIn("demo", "secret");
        REQUIRE(auth.ok());
        session.signedIn(auth.value());
        CHECK(session.fetchImage(server.baseUrl() + "/Items/m-1/Images/Primary", {}).ok());
    }
    CHECK(test::readFile(store).find("tok-demo") != std::string::npos);
    app::AppSession again(testHttp(), store);
    REQUIRE(again.load().ok());
    CHECK(app::startTarget(again) == app::StartTarget::Home);
    REQUIRE(again.client());
    CHECK(again.client()->session().token == "tok-demo");
    CHECK(again.client()->currentUser().ok());
    again.forgetToken();
    CHECK(test::readFile(store).find("tok-demo") == std::string::npos);
    app::AppSession third(testHttp(), store);
    REQUIRE(third.load().ok());
    CHECK(app::startTarget(third) == app::StartTarget::Servers);
}

TEST_CASE("choosing a saved user on another server is where the next start goes") {
    test::FakeJellyfin server;
    TempDir dir;
    const std::string store = dir.path + "/servers.json";
    {
        app::AppSession session(testHttp(), store);
        REQUIRE(session.load().ok());
        auto& first = jellyfin::upsertServer(session.data(), "first");
        jellyfin::rememberUrl(first, "http://192.0.2.1:8096");
        jellyfin::upsertUser(first, "u-first").token = "tok-first";
        auto& second = jellyfin::upsertServer(session.data(), "second");
        jellyfin::rememberUrl(second, server.baseUrl());
        jellyfin::upsertUser(second, "u-second").token = "tok-second";
        session.data().lastServerId = "first";
        session.data().lastUserId = "u-first";
        REQUIRE(session.save());
        session.useServer("second", server.baseUrl());
        const jellyfin::StoredUser chosen = *jellyfin::findServer(session.data(), "second")->users.data();
        session.useStoredUser(chosen);
    }
    app::AppSession again(testHttp(), store);
    REQUIRE(again.load().ok());
    CHECK(app::startTarget(again) == app::StartTarget::Home);
    REQUIRE(again.client());
    CHECK(again.client()->session().token == "tok-second");
}

TEST_CASE("a stored server without a usable address starts at the servers screen") {
    TempDir dir;
    app::AppSession session(testHttp(), dir.path + "/servers.json");
    REQUIRE(session.load().ok());
    auto& stored = jellyfin::upsertServer(session.data(), "srv");
    jellyfin::upsertUser(stored, "u").token = "t";
    session.data().lastServerId = "srv";
    session.data().lastUserId = "u";
    CHECK(app::startTarget(session) == app::StartTarget::Servers);
}

namespace {

std::string fileText(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::string text;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, n);
    }
    std::fclose(file);
    return text;
}

jellyfin::Connection connectionTo(const std::string& url) {
    jellyfin::Connection connection;
    connection.baseUrl = url;
    connection.info.id = "f1e2d3c4b5a6";
    connection.info.name = "Test Server";
    return connection;
}

}  // namespace

TEST_CASE("without a data folder the session never writes, and still has a device id") {
    TempDir dir;
    char previous[4096];
    REQUIRE(getcwd(previous, sizeof(previous)) != nullptr);
    REQUIRE(chdir(dir.path.c_str()) == 0);
    {
        app::AppSession session(testHttp(), "");
        CHECK_FALSE(session.load().ok());
        CHECK_FALSE(session.data().deviceId.empty());
        CHECK_FALSE(session.save());
    }
    REQUIRE(chdir(previous) == 0);
    CHECK(std::filesystem::is_empty(dir.path));
}

TEST_CASE("an unreadable servers.json is never overwritten") {
    if (geteuid() == 0) {
        return;  // root reads anything
    }
    TempDir dir;
    const std::string store = dir.path + "/servers.json";
    { std::FILE* file = std::fopen(store.c_str(), "wb"); std::fputs("{\"version\":1,\"deviceId\":\"keep\"}", file); std::fclose(file); }
    REQUIRE(chmod(store.c_str(), 0) == 0);
    app::AppSession session(testHttp(), store);
    CHECK_FALSE(session.load().ok());
    CHECK_FALSE(session.data().deviceId.empty());
    CHECK_FALSE(session.save());
    REQUIRE(chmod(store.c_str(), 0600) == 0);
    CHECK(fileText(store).find("keep") != std::string::npos);
}

TEST_CASE("saved tokens are only used at addresses the server was already known by") {
    test::FakeJellyfin server;
    TempDir dir;
    const std::string store = dir.path + "/servers.json";
    app::AppSession session(testHttp(), store);
    REQUIRE(session.load().ok());
    auto& stored = jellyfin::upsertServer(session.data(), "f1e2d3c4b5a6");
    jellyfin::rememberUrl(stored, "http://10.0.0.1:8096");
    jellyfin::upsertUser(stored, "u-demo").token = "tok-demo";
    REQUIRE(session.save());

    session.openServer(connectionTo(server.baseUrl()), "");  // e.g. a discovery reply claiming that id
    CHECK_FALSE(session.addressKnown());
    CHECK(session.server()->lastUrl == "http://10.0.0.1:8096");
    CHECK(fileText(store).find(server.baseUrl()) == std::string::npos);

    const auto auth = session.client()->signIn("demo", "secret");
    REQUIRE(auth.ok());
    session.signedIn(auth.value());
    CHECK(session.addressKnown());
    CHECK(session.server()->lastUrl == server.baseUrl());

    session.openServer(connectionTo("http://10.0.0.1:8096"), "");
    CHECK(session.addressKnown());
}
