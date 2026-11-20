#include <doctest/doctest.h>

#include "platform/remote.hpp"

using namespace playfin::platform;

TEST_CASE("remote: a button pressed is an action down, released an action up, held nothing") {
    RemoteButtons remote;
    auto events = remote.update(RemoteButtons::kDown);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == Event::Type::ActionDown);
    CHECK(events[0].action == Action::Down);
    CHECK(remote.update(RemoteButtons::kDown).empty());
    events = remote.update(0);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == Event::Type::ActionUp);
    CHECK(events[0].action == Action::Down);
    CHECK(remote.update(0).empty());
}

TEST_CASE("remote: its buttons are the controller's: cross confirms, circle goes back") {
    const struct {
        std::uint32_t bit;
        Action action;
    } cases[] = {
        {RemoteButtons::kUp, Action::Up},          {RemoteButtons::kLeft, Action::Left},
        {RemoteButtons::kRight, Action::Right},    {RemoteButtons::kCross, Action::Confirm},
        {RemoteButtons::kCircle, Action::Back},    {RemoteButtons::kTriangle, Action::Options},
        {RemoteButtons::kSquare, Action::Context}, {RemoteButtons::kOptions, Action::Menu},
        {RemoteButtons::kL1, Action::L1},          {RemoteButtons::kR1, Action::R1},
        {RemoteButtons::kL2, Action::L2},          {RemoteButtons::kR2, Action::R2},
    };
    for (const auto& c : cases) {
        RemoteButtons remote;
        const auto events = remote.update(c.bit);
        REQUIRE(events.size() == 1);
        CHECK(events[0].action == c.action);
    }
}

TEST_CASE("remote: OK and Back come as key codes, not buttons; the media keys too") {
    RemoteButtons remote;
    auto events = remote.update(0, RemoteButtons::kKeyOk);  // seen on a PS4: buttons 0, key 13
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == Event::Type::ActionDown);
    CHECK(events[0].action == Action::Confirm);
    CHECK(remote.update(0, RemoteButtons::kKeyOk).empty());  // held
    events = remote.update(0, 0);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == Event::Type::ActionUp);
    CHECK(events[0].action == Action::Confirm);
    const struct {
        std::uint8_t key;
        Action action;
    } cases[] = {
        {RemoteButtons::kKeyBack, Action::Back},       {RemoteButtons::kKeyPlay, Action::Confirm},
        {RemoteButtons::kKeyPause, Action::Confirm},   {RemoteButtons::kKeyPlayPause, Action::Confirm},
        {RemoteButtons::kKeyMenu, Action::Options},    {RemoteButtons::kKeyContextMenu, Action::Options},
        {RemoteButtons::kKeyRewind, Action::L1},       {RemoteButtons::kKeyFastForward, Action::R1},
    };
    for (const auto& c : cases) {
        RemoteButtons one;
        const auto down = one.update(0, c.key);
        REQUIRE(down.size() == 1);
        CHECK(down[0].action == c.action);
    }
    RemoteButtons unknown;
    CHECK(unknown.update(0, 200).empty());  // a key Playfin has no use for
}

TEST_CASE("remote: one key to another without a gap releases the first; unknown bits give nothing") {
    RemoteButtons remote;
    remote.update(RemoteButtons::kLeft);
    const auto events = remote.update(RemoteButtons::kRight);
    REQUIRE(events.size() == 2);
    int downs = 0;
    for (const Event& event : events) {
        if (event.type == Event::Type::ActionDown) {
            ++downs;
            CHECK(event.action == Action::Right);
        } else {
            CHECK(event.action == Action::Left);
        }
    }
    CHECK(downs == 1);
    CHECK(remote.update(RemoteButtons::kRight | 0x80000000u | 0x1u).empty());
    const auto released = remote.release();
    REQUIRE(released.size() == 1);
    CHECK(released[0].type == Event::Type::ActionUp);
}
