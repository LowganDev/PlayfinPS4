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

double seekStep(std::int64_t heldMs) { return heldMs >= 2000 ? 30.0 : 10.0; }

}  // namespace playfin::player
