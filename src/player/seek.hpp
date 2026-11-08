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

// Seconds per ← / → (or L1 / R1) press: 10, then 30 once the button has been held for 2 s, 60 after 4 s, 120 after
// 6 s.
double seekStep(std::int64_t heldMs);

// Repeats a held seek button (← / → or L1 / R1, all alike): first after kDelayMs, then every kIntervalMs, on time
// rather than on frames, so a slow frame (a video playing) neither slows the seeking down nor makes one button
// faster than another. `direction` is -1 or 1.
class ButtonRepeat {
public:
    static constexpr std::int64_t kDelayMs = 400;
    static constexpr std::int64_t kIntervalMs = 200;
    static constexpr int kMaxCatchUp = 5;  // repeats given at most by one late poll

    void press(int direction, std::int64_t nowMs);
    void release() { direction_ = 0; }
    int direction() const { return direction_; }
    // The repeats due since the last poll (0 when none).
    int poll(std::int64_t nowMs);

private:
    int direction_ = 0;
    std::int64_t nextMs_ = 0;
};

// L2 / R2 held kHoldMs for the previous / next video: nothing happens on a short press (a trigger is easily
// touched). `direction` is -1 or 1; 0 when nothing is held.
class HoldToSkip {
public:
    static constexpr std::int64_t kHoldMs = 3000;

    void press(int direction, std::int64_t nowMs);
    void release() { direction_ = 0; }
    int direction() const { return direction_; }
    // 0 to 1 while held
    double progress(std::int64_t nowMs) const;
    // true once, when held long enough (then released)
    bool due(std::int64_t nowMs);

private:
    int direction_ = 0;
    std::int64_t startMs_ = 0;
};

}  // namespace playfin::player
