#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "player/queue.hpp"

using namespace playfin;
using player::Queue;
using player::Repeat;

namespace {
std::vector<jellyfin::Item> tracks(int count) {
    std::vector<jellyfin::Item> out;
    for (int i = 0; i < count; ++i) {
        jellyfin::Item item;
        item.id = "t" + std::to_string(i);
        out.push_back(item);
    }
    return out;
}

std::string currentId(const Queue& queue) { return queue.current() != nullptr ? queue.current()->id : "none"; }
}  // namespace

TEST_CASE("a queue starts at the chosen track and moves forward") {
    Queue queue;
    CHECK(queue.empty());
    CHECK(queue.current() == nullptr);
    queue.set(tracks(4), 1, false);
    CHECK(currentId(queue) == "t1");
    CHECK(queue.next(false));
    CHECK(currentId(queue) == "t2");
    CHECK(queue.next(true));
    CHECK(currentId(queue) == "t3");
    CHECK_FALSE(queue.next(false));  // the end, repeat off
    CHECK(currentId(queue) == "t3");
    const auto upcoming = queue.upcoming(10);
    CHECK(upcoming.empty());
}

TEST_CASE("previous restarts the track after 3 s, else goes back") {
    Queue queue;
    queue.set(tracks(3), 1, false);
    CHECK(queue.previous(3.5) == Queue::Back::Restart);
    CHECK(currentId(queue) == "t1");
    CHECK(queue.previous(1.0) == Queue::Back::Moved);
    CHECK(currentId(queue) == "t0");
    CHECK(queue.previous(1.0) == Queue::Back::Restart);  // the first track, repeat off
    queue.setRepeat(Repeat::All);
    CHECK(queue.previous(1.0) == Queue::Back::Moved);  // wraps to the last
    CHECK(currentId(queue) == "t2");
}

TEST_CASE("repeat all wraps, repeat one stays on the track unless the user skips") {
    Queue queue;
    queue.set(tracks(2), 1, false);
    queue.setRepeat(Repeat::All);
    CHECK(queue.next(false));
    CHECK(currentId(queue) == "t0");
    queue.setRepeat(Repeat::One);
    CHECK(queue.next(false));
    CHECK(currentId(queue) == "t0");
    CHECK(queue.next(true));
    CHECK(currentId(queue) == "t1");
    CHECK(queue.next(true));  // a user skip at the end wraps with repeat one
    CHECK(currentId(queue) == "t0");
}

TEST_CASE("shuffle keeps the current track first and plays every track once") {
    Queue queue(42);
    queue.set(tracks(10), 4, true);
    CHECK(queue.shuffle());
    CHECK(currentId(queue) == "t4");
    std::set<std::string> seen{"t4"};
    std::vector<std::string> order{"t4"};
    while (queue.next(false)) {
        seen.insert(currentId(queue));
        order.push_back(currentId(queue));
    }
    CHECK(seen.size() == 10);
    CHECK(order.size() == 10);
    std::vector<std::string> sorted = order;
    std::sort(sorted.begin() + 1, sorted.end());
    CHECK(order != sorted);  // actually shuffled (seed 42)
}

TEST_CASE("turning shuffle off returns to the album order after the current track") {
    Queue queue(7);
    queue.set(tracks(6), 0, true);
    queue.next(false);
    const std::string playing = currentId(queue);
    queue.setShuffle(false);
    CHECK(currentId(queue) == playing);
    const int index = std::stoi(playing.substr(1));
    const auto upcoming = queue.upcoming(10);
    REQUIRE(static_cast<int>(upcoming.size()) == 5 - index);
    if (!upcoming.empty()) {
        CHECK(upcoming.front()->id == "t" + std::to_string(index + 1));
    }
}

TEST_CASE("jumping to an upcoming track") {
    Queue queue;
    queue.set(tracks(5), 0, false);
    const auto upcoming = queue.upcoming(3);
    REQUIRE(upcoming.size() == 3);
    CHECK(upcoming[0]->id == "t1");
    CHECK(upcoming[2]->id == "t3");
    queue.jump(2);  // the second upcoming track
    CHECK(currentId(queue) == "t2");
    queue.jump(10);  // out of range: nothing
    CHECK(currentId(queue) == "t2");
}

TEST_CASE("an empty set or a start out of range") {
    Queue queue;
    queue.set({}, 0, false);
    CHECK(queue.empty());
    CHECK_FALSE(queue.next(true));
    queue.set(tracks(3), 9, false);
    CHECK(currentId(queue) == "t0");
}
