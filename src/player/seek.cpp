#include "player/seek.hpp"

#include <algorithm>

namespace playfin::player {

namespace {
constexpr std::int64_t kSettleMs = 300;
}

void SeekAccumulator::press(double deltaSeconds, std::int64_t nowMs, double position, double duration) {
    const double from = pending_ ? target_ : position;
    const double last = std::max(0.0, duration - 1.0);
    target_ = std::clamp(from + deltaSeconds, 0.0, last);
    pending_ = true;
    lastPressMs_ = nowMs;
}

std::optional<double> SeekAccumulator::due(std::int64_t nowMs) {
    if (!pending_ || nowMs - lastPressMs_ < kSettleMs) {
        return std::nullopt;
    }
    pending_ = false;
    return target_;
}

double seekStep(std::int64_t heldMs) {
    if (heldMs >= 6000) {
        return 120.0;
    }
    if (heldMs >= 4000) {
        return 60.0;
    }
    return heldMs >= 2000 ? 30.0 : 10.0;
}

void ButtonRepeat::press(int direction, std::int64_t nowMs) {
    direction_ = direction;
    nextMs_ = nowMs + kDelayMs;
}

std::optional<int> ButtonRepeat::poll(std::int64_t nowMs) {
    if (direction_ == 0 || nowMs < nextMs_) {
        return std::nullopt;
    }
    nextMs_ = nowMs + kIntervalMs;
    return direction_;
}

void HoldToSkip::press(int direction, std::int64_t nowMs) {
    direction_ = direction;
    startMs_ = nowMs;
}

double HoldToSkip::progress(std::int64_t nowMs) const {
    if (direction_ == 0) {
        return 0;
    }
    const double done = static_cast<double>(nowMs - startMs_) / static_cast<double>(kHoldMs);
    return done < 0 ? 0 : done > 1 ? 1 : done;
}

bool HoldToSkip::due(std::int64_t nowMs) {
    if (direction_ == 0 || nowMs - startMs_ < kHoldMs) {
        return false;
    }
    direction_ = 0;
    return true;
}

}  // namespace playfin::player
