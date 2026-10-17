#include <doctest/doctest.h>

#include "testing/script.hpp"

using playfin::platform::Action;
using playfin::testing::parseScript;
using Kind = playfin::testing::Step::Kind;

TEST_CASE("a valid script is parsed in order, comments and blank lines skipped") {
    const auto script = parseScript("# demo\nwait 2\n\npress Down\nscreenshot home_1\nquit\n");
    REQUIRE(script.ok());
    REQUIRE(script.steps.size() == 4);
    CHECK(script.steps[0].kind == Kind::Wait);
    CHECK(script.steps[0].frames == 2);
    CHECK(script.steps[1].kind == Kind::Press);
    CHECK(script.steps[1].action == Action::Down);
    CHECK(script.steps[2].kind == Kind::Screenshot);
    CHECK(script.steps[2].name == "home_1");
    CHECK(script.steps[3].kind == Kind::Quit);
}

TEST_CASE("errors report the line") {
    auto script = parseScript("wait 1\njump\n");
    CHECK_FALSE(script.ok());
    CHECK(script.errorLine == 2);
    CHECK_FALSE(parseScript("press Fly").ok());
    CHECK_FALSE(parseScript("wait -3").ok());
    CHECK_FALSE(parseScript("wait abc").ok());
    CHECK_FALSE(parseScript("wait 100000").ok());
    CHECK_FALSE(parseScript("screenshot").ok());
}

TEST_CASE("screenshot names cannot be paths") {
    for (const char* bad : {"screenshot ../x", "screenshot /etc/x", "screenshot a/b", "screenshot A.png",
                            "screenshot a b"}) {
        INFO(bad);
        CHECK_FALSE(parseScript(bad).ok());
    }
}

TEST_CASE("type sends the rest of the line as text") {
    const auto script = parseScript("type Ana \xE6\x97\xA5\xE6\x9C\xAC\n");
    REQUIRE(script.ok());
    REQUIRE(script.steps.size() == 1);
    CHECK(script.steps[0].kind == Kind::Type);
    CHECK(script.steps[0].name == "Ana \xE6\x97\xA5\xE6\x9C\xAC");
    CHECK_FALSE(parseScript("type").ok());
}
