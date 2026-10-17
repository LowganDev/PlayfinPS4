#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "jellyfin/models.hpp"

namespace playfin::player {

enum class Repeat { Off, All, One };

// The music queue: the tracks in their album or playlist order, the order they play in (shuffled or not), the one
// playing. Pure logic (no playback).
class Queue {
public:
    explicit Queue(std::uint32_t seed = std::random_device{}()) : rng_(seed) {}

    // Plays `items` from `start` (0 when out of range); shuffled: `start` first, the others in a random order.
    void set(std::vector<jellyfin::Item> items, int start, bool shuffle);
    bool empty() const { return items_.empty(); }
    const jellyfin::Item* current() const;
    const std::vector<jellyfin::Item>& items() const { return items_; }

    // The next track: false at the end (repeat off). Repeat one keeps the track when it ended by itself, a user's
    // skip moves on (and wraps).
    bool next(bool userAction);
    enum class Back { Restart, Moved };
    // After 3 s of the track, or at the first track (repeat off): restart it; else the previous one.
    Back previous(double positionSeconds);
    // The `offset`-th upcoming track (1 = the next one) becomes the current one.
    void jump(int offset);
    std::vector<const jellyfin::Item*> upcoming(std::size_t max) const;

    void setShuffle(bool on);
    bool shuffle() const { return shuffle_; }
    void setRepeat(Repeat repeat) { repeat_ = repeat; }
    Repeat repeat() const { return repeat_; }

private:
    std::vector<jellyfin::Item> items_;
    std::vector<std::size_t> order_;  // indices into items_
    std::size_t position_ = 0;        // into order_
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    std::mt19937 rng_;
};

}  // namespace playfin::player
