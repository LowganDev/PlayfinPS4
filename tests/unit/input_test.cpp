#include <doctest/doctest.h>

#include "platform/input.hpp"

using playfin::platform::Action;
using playfin::platform::KeyRepeat;

TEST_CASE("action names round-trip") {
    for (int i = 0; i < playfin::platform::kActionCount; ++i) {
        const auto action = static_cast<Action>(i);
        CHECK(playfin::platform::actionFromName(playfin::platform::actionName(action)) == action);
    }
    CHECK_FALSE(playfin::platform::actionFromName("Jump").has_value());
    CHECK_FALSE(playfin::platform::actionFromName("").has_value());
}

TEST_CASE("a held direction repeats after the delay, then at the interval") {
    KeyRepeat repeat;
    repeat.press(Action::Down, 0);
    CHECK_FALSE(repeat.poll(399).has_value());
    CHECK(repeat.poll(400) == Action::Down);
    CHECK_FALSE(repeat.poll(479).has_value());
    CHECK(repeat.poll(480) == Action::Down);
    repeat.release(Action::Down);
    CHECK_FALSE(repeat.poll(1000).has_value());
}

TEST_CASE("buttons never repeat") {
    KeyRepeat repeat;
    repeat.press(Action::Confirm, 0);
    CHECK_FALSE(repeat.poll(5000).has_value());
}

TEST_CASE("the last pressed direction wins and a long frame does not burst") {
    KeyRepeat repeat;
    repeat.press(Action::Left, 0);
    repeat.press(Action::Right, 100);
    CHECK_FALSE(repeat.poll(499).has_value());
    CHECK(repeat.poll(500) == Action::Right);
    repeat.release(Action::Left);
    CHECK(repeat.poll(2000) == Action::Right);
    CHECK_FALSE(repeat.poll(2001).has_value());
}
