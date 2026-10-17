#include <doctest/doctest.h>

#include <string>

#include "app/shell/licenses.hpp"
#include "core/file_io.hpp"

using namespace playfin;

namespace {
const std::string kDir = std::string(PLAYFIN_SOURCE_DIR) + "/resources/licenses";

std::string read(const std::string& path) {
    std::string text;
    REQUIRE(core::readSmallFile(path, 4u << 20, text) == core::ReadStatus::Ok);
    return text;
}
}  // namespace

TEST_CASE("the licence manifest is parsed line by line") {
    const auto entries = app::parseLicenseManifest(
        "# comment\n\nFFmpeg|7.1.5|LGPL-3.0-or-later|ffmpeg.txt,LGPL-3.0.txt|avcodec,avutil\nbroken line\n");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "FFmpeg");
    CHECK(entries[0].version == "7.1.5");
    CHECK(entries[0].license == "LGPL-3.0-or-later");
    CHECK(entries[0].files == std::vector<std::string>{"ffmpeg.txt", "LGPL-3.0.txt"});
    CHECK(entries[0].libraries == std::vector<std::string>{"avcodec", "avutil"});
}

TEST_CASE("every component shipped has its licence text and is listed in THIRD_PARTY_LICENSES") {
    const auto entries = app::parseLicenseManifest(read(kDir + "/manifest.txt"));
    REQUIRE(entries.size() >= 20);
    const std::string summary = read(std::string(PLAYFIN_SOURCE_DIR) + "/THIRD_PARTY_LICENSES");
    for (const auto& entry : entries) {
        CAPTURE(entry.name);
        CHECK(summary.find(entry.name) != std::string::npos);
        REQUIRE_FALSE(entry.files.empty());
        for (const auto& file : entry.files) {
            CAPTURE(file);
            CHECK(read(kDir + "/" + file).size() > 100);
        }
    }
}
