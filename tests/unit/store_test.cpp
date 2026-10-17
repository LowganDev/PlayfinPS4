#include <doctest/doctest.h>

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "jellyfin/store.hpp"
#include "support/files.hpp"

using namespace playfin::jellyfin;
using playfin::core::ErrorKind;

namespace {

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/playfin-store-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

}  // namespace

TEST_CASE("device ids are random UUID v4") {
    const std::string a = newDeviceId();
    const std::string b = newDeviceId();
    CHECK(a != b);
    REQUIRE(a.size() == 36);
    CHECK(a[8] == '-');
    CHECK(a[13] == '-');
    CHECK(a[18] == '-');
    CHECK(a[23] == '-');
    CHECK(a[14] == '4');
    CHECK(std::string("89ab").find(a[19]) != std::string::npos);
}

TEST_CASE("a missing file starts empty with a device id, saved with private permissions") {
    TempDir dir;
    ServerStore store(dir.path + "/servers.json");
    const auto data = store.load();
    REQUIRE(data.ok());
    CHECK(data.value().servers.empty());
    CHECK(data.value().deviceId.size() == 36);
    struct stat info {};
    REQUIRE(stat(store.path().c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
    CHECK(store.load().value().deviceId == data.value().deviceId);
}

TEST_CASE("servers, pins and users survive a round trip") {
    TempDir dir;
    ServerStore store(dir.path + "/servers.json");
    StoreData data;
    data.deviceId = "dev";
    data.lastServerId = "srv";
    data.lastUserId = "u1";
    StoredServer& server = upsertServer(data, "srv");
    server.name = "Home \"NAS\"\nmédia";
    rememberUrl(server, "http://192.168.1.2:8096");
    rememberUrl(server, "https://media.example.com");
    server.pin = "sha256//abc=";
    StoredUser& user = upsertUser(server, "u1");
    user.name = "demo";
    user.token = "tok";
    REQUIRE(store.save(data).ok());
    const auto loaded = store.load();
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().servers.size() == 1);
    const StoredServer& back = loaded.value().servers[0];
    CHECK(back.name == "Home \"NAS\"\nmédia");
    CHECK(back.lastUrl == "https://media.example.com");
    CHECK(back.urls == std::vector<std::string>{"https://media.example.com", "http://192.168.1.2:8096"});
    CHECK(back.pin == "sha256//abc=");
    REQUIRE(back.users.size() == 1);
    CHECK(back.users[0].token == "tok");
    CHECK(loaded.value().lastServerId == "srv");
    CHECK(loaded.value().lastUserId == "u1");
}

TEST_CASE("a corrupt file is set aside and the store starts empty") {
    TempDir dir;
    const std::string path = dir.path + "/servers.json";
    std::ofstream(path) << "{not json";
    const auto data = ServerStore(path).load();
    REQUIRE(data.ok());
    CHECK(data.value().servers.empty());
    CHECK(playfin::test::readFile(path + ".bad") == "{not json");
}

TEST_CASE("a corrupt file that cannot be set aside is kept as it is") {
    TempDir dir;
    const std::string path = dir.path + "/servers.json";
    std::ofstream(path) << "{not json";
    std::filesystem::create_directories(path + ".bad/taken");  // the rename fails
    const auto data = ServerStore(path).load();
    CHECK_FALSE(data.ok());
    CHECK(playfin::test::readFile(path) == "{not json");
}

TEST_CASE("an unreadable store is a Storage error") {
    TempDir dir;
    const std::string path = dir.path + "/servers.json";
    std::filesystem::create_directory(path);
    const auto data = ServerStore(path).load();
    REQUIRE_FALSE(data.ok());
    CHECK(data.error().kind == ErrorKind::Storage);
}

TEST_CASE("rememberUrl keeps the last working address first, without duplicates, at most five") {
    StoredServer server;
    for (const char* url : {"a", "b", "c", "d", "e", "f", "c"}) {
        rememberUrl(server, url);
    }
    CHECK(server.lastUrl == "c");
    CHECK(server.urls == std::vector<std::string>{"c", "f", "e", "d", "b"});
}

TEST_CASE("find and upsert") {
    StoreData data;
    CHECK(findServer(data, "x") == nullptr);
    upsertServer(data, "x").name = "X";
    upsertServer(data, "x").name = "X2";
    REQUIRE(data.servers.size() == 1);
    CHECK(findServer(data, "x")->name == "X2");
    StoredServer& server = data.servers[0];
    upsertUser(server, "u").token = "t1";
    upsertUser(server, "u").token = "t2";
    REQUIRE(server.users.size() == 1);
    CHECK(server.users[0].token == "t2");
}
