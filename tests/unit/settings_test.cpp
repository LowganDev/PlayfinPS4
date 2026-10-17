#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/settings.hpp"
#include "core/file_io.hpp"
#include "ui/theme.hpp"

using namespace playfin;

namespace {

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/playfin-settings-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

}  // namespace

TEST_CASE("writeFileAtomic writes a private file that readSmallFile reads back") {
    TempDir dir;
    const std::string path = dir.path + "/a.json";
    REQUIRE(core::writeFileAtomic(path, "hello"));
    std::string text;
    CHECK(core::readSmallFile(path, 1024, text) == core::ReadStatus::Ok);
    CHECK(text == "hello");
    struct stat info {};
    REQUIRE(::stat(path.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
    CHECK(core::readSmallFile(dir.path + "/missing", 1024, text) == core::ReadStatus::Missing);
    CHECK(core::readSmallFile(path, 2, text) == core::ReadStatus::Failed);  // larger than allowed
}

TEST_CASE("settings round trip") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    app::Settings settings;
    settings.accent = "red";
    settings.maxBitrate = 4000000;
    settings.language = "de";
    settings.recentSearches = {"dune", "alien"};
    REQUIRE(app::saveSettings(path, settings));
    const app::Settings loaded = app::loadSettings(path);
    CHECK(loaded.accent == "red");
    CHECK(loaded.maxBitrate == 4000000);
    CHECK(loaded.language == "de");
    CHECK(loaded.recentSearches == std::vector<std::string>{"dune", "alien"});
}

TEST_CASE("missing settings give the defaults") {
    TempDir dir;
    const app::Settings settings = app::loadSettings(dir.path + "/settings.json");
    CHECK(settings.accent == "playstation");
    CHECK(settings.maxBitrate == 40000000);
    CHECK(settings.language.empty());
    CHECK(settings.recentSearches.empty());
}

TEST_CASE("damaged settings are kept aside and the defaults used") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    {
        std::ofstream out(path);
        out << "{not json";
    }
    const app::Settings settings = app::loadSettings(path);
    CHECK(settings.accent == "playstation");
    CHECK(std::filesystem::exists(path + ".bad"));
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("out of range values fall back to the defaults") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"accent":7,"maxBitrate":-5,"language":"xx-unknown","recentSearches":["a",3,"b"]})";
    }
    const app::Settings settings = app::loadSettings(path);
    CHECK(settings.accent == "playstation");
    CHECK(settings.maxBitrate == 40000000);
    CHECK(settings.recentSearches == std::vector<std::string>{"a", "b"});
}

TEST_CASE("an unknown accent id gives the PlayStation blue") {
    CHECK(std::string(app::accentById("violet").id) == "violet");
    CHECK(std::string(app::accentById("nope").id) == "playstation");
    CHECK(std::string(app::accentById("").id) == "playstation");
}

TEST_CASE("bitrate choices go from Auto (40 Mbit/s) down to 1.5 Mbit/s") {
    CHECK(app::bitrateChoices() == std::vector<std::int64_t>{40000000, 20000000, 10000000, 4000000, 1500000});
}

TEST_CASE("recent searches: trimmed, newest first, no duplicates, at most 8") {
    app::Settings settings;
    app::addRecentSearch(settings, "   ");
    CHECK(settings.recentSearches.empty());
    app::addRecentSearch(settings, " a ");
    app::addRecentSearch(settings, "b");
    app::addRecentSearch(settings, "a");
    CHECK(settings.recentSearches == std::vector<std::string>{"a", "b"});
    for (int i = 0; i < 10; ++i) {
        app::addRecentSearch(settings, "t" + std::to_string(i));
    }
    REQUIRE(settings.recentSearches.size() == 8);
    CHECK(settings.recentSearches.front() == "t9");
    CHECK(settings.recentSearches.back() == "t2");
}
