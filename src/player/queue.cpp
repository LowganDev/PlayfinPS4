#include "player/queue.hpp"

#include <algorithm>
#include <numeric>

namespace playfin::player {

namespace {
constexpr double kRestartAfterSeconds = 3;
}

void Queue::set(std::vector<jellyfin::Item> items, int start, bool shuffle) {
    items_ = std::move(items);
    const bool inRange = start >= 0 && static_cast<std::size_t>(start) < items_.size();
    const std::size_t first = inRange ? static_cast<std::size_t>(start) : 0;
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    position_ = first;
    shuffle_ = false;
    if (shuffle) {
        setShuffle(true);
    }
}

const jellyfin::Item* Queue::current() const {
    return position_ < order_.size() ? &items_[order_[position_]] : nullptr;
}

bool Queue::next(bool userAction) {
    if (order_.empty()) {
        return false;
    }
    if (repeat_ == Repeat::One && !userAction) {
        return true;
    }
    if (position_ + 1 < order_.size()) {
        ++position_;
        return true;
    }
    if (repeat_ != Repeat::Off) {
        position_ = 0;
        return true;
    }
    return false;
}

Queue::Back Queue::previous(double positionSeconds) {
    if (order_.empty() || positionSeconds > kRestartAfterSeconds) {
        return Back::Restart;
    }
    if (position_ > 0) {
        --position_;
        return Back::Moved;
    }
    if (repeat_ != Repeat::Off && order_.size() > 1) {
        position_ = order_.size() - 1;
        return Back::Moved;
    }
    return Back::Restart;
}

void Queue::jump(int offset) {
    if (offset > 0 && position_ + static_cast<std::size_t>(offset) < order_.size()) {
        position_ += static_cast<std::size_t>(offset);
    }
}

std::vector<const jellyfin::Item*> Queue::upcoming(std::size_t max) const {
    std::vector<const jellyfin::Item*> out;
    for (std::size_t i = position_ + 1; i < order_.size() && out.size() < max; ++i) {
        out.push_back(&items_[order_[i]]);
    }
    return out;
}

void Queue::setShuffle(bool on) {
    if (order_.empty()) {
        shuffle_ = on;
        return;
    }
    const std::size_t playing = order_[position_];
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    if (on) {
        // the playing track first, then the others shuffled
        std::swap(order_[0], order_[playing]);
        std::shuffle(order_.begin() + 1, order_.end(), rng_);
        position_ = 0;
    } else {
        position_ = playing;
    }
    shuffle_ = on;
}

}  // namespace playfin::player
