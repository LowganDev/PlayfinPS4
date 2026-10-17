#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/shell/guide.hpp"
#include "jellyfin/models.hpp"
#include "ui/i18n.hpp"

using namespace playfin;

namespace {
jellyfin::Item program(const char* name, const char* start, const char* end) {
    jellyfin::Item item;
    item.name = name;
    item.type = jellyfin::ItemType::Program;
    item.startUtc = jellyfin::parseUtc(start);
    item.endUtc = jellyfin::parseUtc(end);
    return item;
}

struct English {
    English() { ui::translator().load(std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n", "en"); }
};
}  // namespace

TEST_CASE("guide times are the local 24-hour clock") {
    const std::int64_t t = jellyfin::parseUtc("2026-10-16T19:30:00Z");
    CHECK(app::clock24(t, 120) == "21:30");
    CHECK(app::clock24(t, -300) == "14:30");
    CHECK(app::clock24(t, 0) == "19:30");
    CHECK(app::clock24(jellyfin::parseUtc("2026-10-16T23:05:00Z"), 60) == "00:05");
    CHECK(app::timeRange(program("x", "2026-10-16T19:30:00Z", "2026-10-16T20:15:00Z"), 120) ==
          "21:30 \xE2\x80\x93 22:15");
}

TEST_CASE("guide days: today, tomorrow, then the weekday and the date, in local time") {
    English english;
    const std::int64_t now = jellyfin::parseUtc("2026-10-16T21:50:00Z");  // 23:50 in Paris (UTC+2)
    CHECK(app::dayHeading(jellyfin::parseUtc("2026-10-16T21:00:00Z"), now, 120) == "Today");
    CHECK(app::dayHeading(jellyfin::parseUtc("2026-10-16T22:30:00Z"), now, 120) == "Tomorrow");  // 00:30 local
    CHECK(app::dayHeading(jellyfin::parseUtc("2026-10-16T22:30:00Z"), now, 0) == "Today");       // 22:30 in UTC
    CHECK(app::dayHeading(jellyfin::parseUtc("2026-10-18T10:00:00Z"), now, 120) == "Sunday 18");
    CHECK(app::dayHeading(jellyfin::parseUtc("2026-10-19T10:00:00Z"), now, -300) == "Monday 19");
}

TEST_CASE("the programme on now and its progress") {
    const std::vector<jellyfin::Item> programs = {
        program("a", "2026-10-16T19:00:00Z", "2026-10-16T19:30:00Z"),
        program("b", "2026-10-16T19:30:00Z", "2026-10-16T20:30:00Z"),
        program("c", "2026-10-16T20:30:00Z", "2026-10-16T21:00:00Z"),
    };
    const std::int64_t now = jellyfin::parseUtc("2026-10-16T19:45:00Z");
    const jellyfin::Item* on = app::onNow(programs, now);
    REQUIRE(on != nullptr);
    CHECK(on->name == "b");
    CHECK(app::airing(*on, now));
    CHECK_FALSE(app::airing(programs[2], now));
    CHECK(app::progress(*on, now) == doctest::Approx(0.25));
    CHECK(app::progress(programs[0], now) == doctest::Approx(1.0));
    CHECK(app::progress(programs[2], now) == doctest::Approx(0.0));
    CHECK(app::onNow(programs, jellyfin::parseUtc("2026-10-16T22:00:00Z")) == nullptr);
}
