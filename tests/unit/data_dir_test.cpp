#include <doctest/doctest.h>

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/data_dir.hpp"

using playfin::core::resolveInside;

TEST_CASE("resolveInside joins plain relative paths") {
    CHECK(resolveInside("/data/Playfin", "playfin.log") == std::string("/data/Playfin/playfin.log"));
    CHECK(resolveInside("/data/Playfin/", "cache/img/a.png") == std::string("/data/Playfin/cache/img/a.png"));
}

TEST_CASE("resolveInside rejects anything that could leave the root") {
    const std::string root = "/data/Playfin";
    for (std::string_view bad : {"", "/etc/passwd", "../x", "a/../../x", "a/./b", "a//b", "a/", ".", "..",
                                 "a\\b", "..\\..\\x"}) {
        INFO("input: " << std::string(bad));
        CHECK_FALSE(resolveInside(root, bad).has_value());
    }
    CHECK_FALSE(resolveInside(root, std::string_view("a\0b", 3)).has_value());
    CHECK_FALSE(resolveInside("", "a").has_value());
}

TEST_CASE("dataPath uses the root set by the platform") {
    playfin::core::setDataRoot("/data/Playfin");
    CHECK(playfin::core::dataRoot() == "/data/Playfin");
    CHECK(playfin::core::dataPath("x.log") == std::string("/data/Playfin/x.log"));
    CHECK_FALSE(playfin::core::dataPath("../x.log").has_value());
}

TEST_CASE("makeDirs creates nested directories and accepts existing ones") {
    char tmpl[] = "/tmp/playfin-test-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string base = tmpl;
    CHECK(playfin::core::makeDirs(base + "/a/b/c"));
    CHECK(std::filesystem::is_directory(base + "/a/b/c"));
    CHECK(playfin::core::makeDirs(base + "/a/b/c"));
    std::filesystem::remove_all(base);
}

namespace {

// The PS4 sandbox refuses mkdir on existing system folders ("/", "/data") with a permission error, not EEXIST.
int refusingMkdir(const char* path, mode_t mode) {
    struct stat info {};
    if (stat(path, &info) == 0) {
        errno = EPERM;
        return -1;
    }
    return mkdir(path, mode);
}

}  // namespace

TEST_CASE("makeDirs skips existing folders even when mkdir refuses them") {
    char tmpl[] = "/tmp/playfin-test-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string base = tmpl;
    CHECK(playfin::core::makeDirsWith(base + "/Playfin/cache", refusingMkdir));
    CHECK(std::filesystem::is_directory(base + "/Playfin/cache"));
    std::filesystem::remove_all(base);
}

TEST_CASE("makeDirsWith gives the folders it creates the requested mode, whatever the umask") {
    char tmpl[] = "/tmp/playfin-test-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string base = tmpl;
    const mode_t previous = umask(022);
    CHECK(playfin::core::makeDirsWith(base + "/Playfin/cache", ::mkdir, 0777));
    umask(previous);
    struct stat info {};
    REQUIRE(stat((base + "/Playfin").c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0777);
    REQUIRE(stat((base + "/Playfin/cache").c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0777);
    std::filesystem::remove_all(base);
}
