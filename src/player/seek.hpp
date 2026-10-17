#pragma once

#include <cstdint>
#include <optional>

namespace playfin::player {

// Seek presses add up while they come; the seek happens once, 300 ms after the last press. Targets stay within
// [0, duration - 1 s].
class SeekAccumulator {
public:
    void press(double deltaSeconds, std::int64_t nowMs, double position, double duration);
    // The target to seek to, once it is time (then forgotten).
    std::optional<double> due(std::int64_t nowMs);
    double target() const { return target_; }
    bool pending() const { return pending_; }

private:
    bool pending_ = false;
    double target_ = 0;
    std::int64_t lastPressMs_ = 0;
};

// Seconds per ← / → press: 10, then 30 once the button has been held for 2 s.
double seekStep(std::int64_t heldMs);

}  // namespace playfin::player
