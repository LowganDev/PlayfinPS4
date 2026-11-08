#pragma once

#include <cstdint>

namespace playfin::ui {

// The UI's steady 30 frames a second. The loop turns with the display (every 16.7 ms); a frame is drawn once
// kIntervalMs (less the loop's slack) passed since the last one, so a quick frame waits for its slot instead of
// showing at 60 then at 30, which looks uneven. Drawing takes up to about 28 ms on the PS4 (software), which a
// steady 60 could never hold.
class FramePacer {
public:
    static constexpr std::uint32_t kIntervalMs = 33;
    static constexpr std::uint32_t kSlackMs = 4;  // the display's slot at 33.3 ms comes back as 33, sometimes 32

    bool due(std::uint32_t nowMs) const { return !drawn_ || nowMs - lastMs_ >= kIntervalMs - kSlackMs; }
    void drew(std::uint32_t nowMs) {
        lastMs_ = nowMs;
        drawn_ = true;
    }

private:
    std::uint32_t lastMs_ = 0;
    bool drawn_ = false;
};

}  // namespace playfin::ui
