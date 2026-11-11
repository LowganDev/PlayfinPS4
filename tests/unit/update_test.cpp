#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "app/update.hpp"
#include "support/files.hpp"

using namespace playfin;

TEST_CASE("the latest release: its version, its package, its size and digest") {
    const auto release = app::parseLatestRelease(test::readFixture("github/release_latest.json"));
    REQUIRE(release.ok());
    CHECK(release.value().version == "1.0.1");
    CHECK(release.value().packageUrl ==
          "https://github.com/LowganDev/PlayfinPS4/releases/download/1.0.1/IV0001-PLFN00000_00-PLFN000000100000.pkg");
    CHECK(release.value().size == 50921472);
    CHECK(release.value().sha256 == "0f4d7d60ed66f9927f9c5b3a5da498a942c2edb0e5c56c1d4fa5a53307560f26");
    CHECK_FALSE(app::parseLatestRelease("{}").ok());
    CHECK_FALSE(app::parseLatestRelease(R"({"tag_name":"2.0.0","assets":[{"name":"x.tar","size":1}]})").ok());
    CHECK_FALSE(app::parseLatestRelease(  // a package without a published digest is not taken
                    R"({"tag_name":"2.0.0","assets":[{"name":"a.pkg","size":5,"browser_download_url":"https://x/a.pkg"}]})")
                    .ok());
    CHECK_FALSE(app::parseLatestRelease(  // only https addresses
                    R"({"tag_name":"2.0.0","assets":[{"name":"a.pkg","size":5,"digest":"sha256:)" +
                    std::string(64, 'a') + R"(","browser_download_url":"http://x/a.pkg"}]})")
                    .ok());
}

TEST_CASE("versions compare part by part; a v prefix is accepted") {
    CHECK(app::newerVersion("2.0.0", "1.0.1"));
    CHECK(app::newerVersion("1.0.10", "1.0.9"));
    CHECK(app::newerVersion("v1.1", "1.0.1"));
    CHECK_FALSE(app::newerVersion("1.0.1", "1.0.1"));
    CHECK_FALSE(app::newerVersion("1.0.0", "1.0.1"));
    CHECK_FALSE(app::newerVersion("garbage", "1.0.1"));
}

TEST_CASE("the package's file name: Playfin-<version>.pkg, nothing that could leave the folder") {
    CHECK(app::packageFileName("2.0.0") == "Playfin-2.0.0.pkg");
    CHECK(app::packageFileName("../../x") == "Playfin-x.pkg");
    CHECK(app::packageFileName("").empty());
}

#include <cstdlib>
#include <filesystem>

#include "core/file_io.hpp"
#include "net/crypto.hpp"
#include "support/fake_server.hpp"

TEST_CASE("the package downloaded is kept only when its size and digest match the release") {
    test::FakeServer server;
    const std::string package(200000, 'p');
    server.http().Get("/a.pkg", [&package](const httplib::Request&, httplib::Response& res) {
        res.set_content(package, "application/octet-stream");
    });
    server.start();
    net::HttpConfig config;
    config.caFile = test::fixturePath("tls/ca.crt");
    const net::HttpClient http(config);
    char tmpl[] = "/tmp/playfin-update-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string dir = tmpl;
    net::Sha256Stream digest;
    digest.update(package.data(), package.size());
    app::Release release;
    release.version = "9.0.0";
    release.packageUrl = server.baseUrl() + "/a.pkg";
    release.size = static_cast<long long>(package.size());
    release.sha256 = net::hexLower(digest.finish());
    long long last = 0;
    const auto ok = app::downloadPackage(http, release, dir + "/Playfin-9.0.0.pkg",
                                         [&](long long received, long long) { last = received; });
    REQUIRE(ok.ok());
    CHECK(last == 200000);
    CHECK(std::filesystem::file_size(dir + "/Playfin-9.0.0.pkg") == 200000);
    release.sha256 = std::string(64, '0');  // another file than the one published
    const auto bad = app::downloadPackage(http, release, dir + "/other.pkg", {});
    REQUIRE_FALSE(bad.ok());
    CHECK_FALSE(std::filesystem::exists(dir + "/other.pkg"));
    CHECK_FALSE(std::filesystem::exists(dir + "/other.pkg.part"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("update packages of this version or older are removed; newer ones and others' packages stay") {
    char tmpl[] = "/tmp/playfin-pkg-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string dir = tmpl;
    for (const char* name : {"Playfin-2.1.0.pkg", "Playfin-2.2.0.pkg", "Playfin-2.3.0.pkg", "Playfin-2.2.0.pkg.part",
                             "IV0001-PLFN00000_00-PLFN000000100000.pkg", "Playtube-1.0.pkg", "Playfin-x.pkg"}) {
        std::ofstream(dir + "/" + name) << "p";
    }
    const int removed = app::removeOldPackages(dir, "2.2.0");
    CHECK(removed == 2);
    CHECK_FALSE(std::filesystem::exists(dir + "/Playfin-2.1.0.pkg"));
    CHECK_FALSE(std::filesystem::exists(dir + "/Playfin-2.2.0.pkg"));  // the one installed: done with
    CHECK(std::filesystem::exists(dir + "/Playfin-2.3.0.pkg"));        // newer: not installed yet
    CHECK(std::filesystem::exists(dir + "/Playfin-2.2.0.pkg.part"));   // not a finished package of ours
    CHECK(std::filesystem::exists(dir + "/IV0001-PLFN00000_00-PLFN000000100000.pkg"));  // put there by the user
    CHECK(std::filesystem::exists(dir + "/Playtube-1.0.pkg"));
    CHECK(std::filesystem::exists(dir + "/Playfin-x.pkg"));
    std::filesystem::remove_all(dir);
}
