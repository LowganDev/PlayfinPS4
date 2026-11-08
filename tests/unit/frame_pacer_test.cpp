#include <doctest/doctest.h>

#include "ui/frame_pacer.hpp"

using playfin::ui::FramePacer;

TEST_CASE("frames are drawn every other display slot: a quick one waits, a slow one goes on the next slot") {
    FramePacer pacer;
    CHECK(pacer.due(1000));  // the first one at once
    pacer.drew(1000);
    CHECK_FALSE(pacer.due(1016));  // 16.7 ms later: too soon, 60 then 30 would look uneven
    CHECK(pacer.due(1033));        // the next 33.3 ms slot
    CHECK(pacer.due(1032));        // the millisecond clock rounds it down
    pacer.drew(1033);
    CHECK(pacer.due(1083));  // a slow frame (50 ms): the next one at once
}

TEST_CASE("the pacer's clock may wrap") {
    FramePacer pacer;
    pacer.drew(0xFFFFFFF0u);
    CHECK_FALSE(pacer.due(0xFFFFFFF0u + 10u));
    CHECK(pacer.due(0xFFFFFFF0u + 40u));  // wrapped past zero
}
