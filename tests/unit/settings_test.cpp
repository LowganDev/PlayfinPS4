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
    settings.volume = 70;
    settings.pictureFit = "fill";
    settings.pictureSize = 90;
    settings.libraryOrder = {"tv", "movies"};
    settings.playMethod = "transcode";
    REQUIRE(app::saveSettings(path, settings));
    const app::Settings loaded = app::loadSettings(path);
    CHECK(loaded.volume == 70);
    CHECK(loaded.pictureFit == "fill");
    CHECK(loaded.pictureSize == 90);
    CHECK(loaded.libraryOrder == std::vector<std::string>{"tv", "movies"});
    CHECK(loaded.playMethod == "transcode");
    CHECK(loaded.accent == "red");
    CHECK(loaded.maxBitrate == 4000000);
    CHECK(loaded.language == "de");
    CHECK(loaded.recentSearches == std::vector<std::string>{"dune", "alien"});
}

TEST_CASE("missing settings give the defaults") {
    TempDir dir;
    const app::Settings settings = app::loadSettings(dir.path + "/settings.json");
    CHECK(settings.theme == "projecteur");
    CHECK(settings.accent == "theme");
    CHECK(settings.maxBitrate == 40000000);
    CHECK(settings.language.empty());
    CHECK(settings.recentSearches.empty());
    CHECK(settings.volume == 100);
    CHECK(settings.pictureFit == "fit");
    CHECK(settings.pictureSize == 100);
}

TEST_CASE("damaged settings are kept aside and the defaults used") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    {
        std::ofstream out(path);
        out << "{not json";
    }
    const app::Settings settings = app::loadSettings(path);
    CHECK(settings.accent == "theme");
    CHECK(std::filesystem::exists(path + ".bad"));
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("out of range values fall back to the defaults") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"accent":7,"maxBitrate":-5,"language":"xx-unknown","recentSearches":["a",3,"b"],)"
               R"("volume":150,"pictureFit":"zoom","pictureSize":40,"playMethod":"fast"})";
    }
    const app::Settings settings = app::loadSettings(path);
    CHECK(settings.volume == 100);
    CHECK(settings.pictureFit == "fit");
    CHECK(settings.pictureSize == 100);
    CHECK(settings.playMethod == "auto");
    CHECK(settings.accent == "theme");
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

TEST_CASE("a file written in pieces appears only once committed; an abandoned one leaves nothing") {
    TempDir dir;
    const std::string path = dir.path + "/big.bin";
    {
        core::FileSink sink(path);
        REQUIRE(sink.ok());
        CHECK(sink.write("abc", 3));
        CHECK(sink.write("def", 3));
        CHECK_FALSE(std::filesystem::exists(path));
        CHECK(sink.commit());
    }
    std::string text;
    CHECK(core::readSmallFile(path, 1024, text) == core::ReadStatus::Ok);
    CHECK(text == "abcdef");
    {
        core::FileSink sink(dir.path + "/gone.bin");
        CHECK(sink.write("x", 1));
    }  // never committed
    CHECK_FALSE(std::filesystem::exists(dir.path + "/gone.bin"));
    CHECK_FALSE(std::filesystem::exists(dir.path + "/gone.bin.part"));
    core::FileSink nowhere(dir.path + "/missing/dir/file.bin");
    CHECK_FALSE(nowhere.ok());
}

TEST_CASE("a 2.1 settings file keeps its accent, on Projecteur") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"accent":"red","volume":80})";
    }
    const app::Settings settings = app::loadSettings(path);
    CHECK(settings.theme == "projecteur");
    CHECK(settings.accent == "red");
}

TEST_CASE("the theme round-trips; an unknown theme gives Projecteur") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    app::Settings settings;
    settings.theme = "violet";
    settings.accent = "theme";
    REQUIRE(app::saveSettings(path, settings));
    const app::Settings loaded = app::loadSettings(path);
    CHECK(loaded.theme == "violet");
    CHECK(loaded.accent == "theme");
    {
        std::ofstream out(path);
        out << R"({"version":1,"theme":"neon","accent":"green"})";
    }
    const app::Settings unknown = app::loadSettings(path);
    CHECK(unknown.theme == "projecteur");
    CHECK(unknown.accent == "green");
}

TEST_CASE("the accent applies on top of the theme") {
    app::Settings settings;
    settings.theme = "mint";
    settings.accent = "red";
    app::applyAppearance(settings);
    CHECK(ui::theme().id == "mint");
    CHECK(ui::theme().accent.r == 0xE5);
    settings.accent = "theme";
    app::applyAppearance(settings);
    CHECK(ui::theme().accent.r == 0xBB);
    app::applyAppearance(app::Settings{});
    CHECK(ui::theme().id == "projecteur");
    CHECK(ui::theme().accent.b == 0xD1);
}

// A film arrives in pieces of a few kilobytes: they are gathered into large writes (on the console every write
// is a system call, and tens of thousands of small ones slow a download down), and nothing is lost at the end.
TEST_CASE("a file written in small pieces is written to disk in large blocks, whole once committed") {
    TempDir dir;
    const std::string path = dir.path + "/film.mkv";
    std::string expected;
    {
        core::FileSink sink(path);
        REQUIRE(sink.ok());
        for (int i = 0; i < 200; ++i) {  // 200 pieces of 1000 bytes: under one block
            const std::string piece(1000, static_cast<char>('a' + i % 26));
            REQUIRE(sink.write(piece.data(), piece.size()));
            expected += piece;
        }
        CHECK(sink.written() == 200000);
        CHECK(std::filesystem::file_size(path + ".part") == 0);  // still gathered
        const std::string big(3 * 1024 * 1024 + 7, 'z');  // more than a block: written through
        REQUIRE(sink.write(big.data(), big.size()));
        expected += big;
        CHECK(std::filesystem::file_size(path + ".part") >= 3u * 1024u * 1024u);
        REQUIRE(sink.commit());
    }
    std::string text;
    REQUIRE(core::readSmallFile(path, 8u << 20, text) == core::ReadStatus::Ok);
    CHECK(text == expected);
}

TEST_CASE("a part file kept, then completed: written on after what it had, then whole") {
    TempDir dir;
    const std::string path = dir.path + "/film.mkv";
    {
        core::FileSink sink(path);
        REQUIRE(sink.write("hello ", 6));
        sink.keep();  // paused: the part stays
    }
    CHECK(std::filesystem::file_size(path + ".part") == 6);
    {
        core::FileSink sink(path, true);
        CHECK(sink.written() == 6);
        REQUIRE(sink.write("world", 5));
        REQUIRE(sink.commit());
    }
    std::string text;
    REQUIRE(core::readSmallFile(path, 1024, text) == core::ReadStatus::Ok);
    CHECK(text == "hello world");
    // the server sends the whole file again: the part starts over
    {
        core::FileSink sink(dir.path + "/again.mkv");
        REQUIRE(sink.write("old", 3));
        sink.keep();
    }
    {
        core::FileSink sink(dir.path + "/again.mkv", true);
        REQUIRE(sink.restart());
        CHECK(sink.written() == 0);
        REQUIRE(sink.write("new", 3));
        REQUIRE(sink.commit());
    }
    REQUIRE(core::readSmallFile(dir.path + "/again.mkv", 1024, text) == core::ReadStatus::Ok);
    CHECK(text == "new");
}

TEST_CASE("subtitle size and background: saved, read back, unknown values give the defaults") {
    TempDir dir;
    const std::string path = dir.path + "/settings.json";
    app::Settings settings;
    CHECK(settings.subtitleSize == "normal");
    CHECK_FALSE(settings.subtitleBackground);
    settings.subtitleSize = "large";
    settings.subtitleBackground = true;
    REQUIRE(app::saveSettings(path, settings));
    const app::Settings loaded = app::loadSettings(path);
    CHECK(loaded.subtitleSize == "large");
    CHECK(loaded.subtitleBackground);
    {
        std::ofstream out(path);
        out << R"({"version":1,"subtitleSize":"huge"})";
    }
    CHECK(app::loadSettings(path).subtitleSize == "normal");
    CHECK(app::subtitleScale("small") == doctest::Approx(0.75));
    CHECK(app::subtitleScale("normal") == doctest::Approx(1.0));
    CHECK(app::subtitleScale("large") == doctest::Approx(1.25));
    CHECK(app::subtitleScale("larger") == doctest::Approx(1.5));
    CHECK(app::subtitleScale("nope") == doctest::Approx(1.0));
}
