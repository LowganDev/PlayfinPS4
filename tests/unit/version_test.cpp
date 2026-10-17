#include <doctest/doctest.h>

#include <string>

#include "core/version.hpp"

TEST_CASE("version matches the CMake project version") {
    // one version: project(Playfin VERSION …) in CMakeLists.txt (the PS4 package's APP_VER is its "XX.YY")
    CHECK(std::string(playfin::core::version()) == PLAYFIN_PROJECT_VERSION);
}
