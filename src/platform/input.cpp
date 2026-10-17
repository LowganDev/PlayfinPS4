#include "platform/input.hpp"

namespace playfin::platform {

namespace {

constexpr const char* kNames[kActionCount] = {"Up",      "Down", "Left", "Right", "Confirm", "Back", "Options",
                                              "Context", "L1",   "R1",   "L2",    "R2",      "Menu"};

}  // namespace

const char* actionName(Action action) { return kNames[static_cast<int>(action)]; }

std::optional<Action> actionFromName(std::string_view name) {
    for (int i = 0; i < kActionCount; ++i) {
        if (name == kNames[i]) {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

bool isDirection(Action action) {
    return action == Action::Up || action == Action::Down || action == Action::Left || action == Action::Right;
}

void KeyRepeat::press(Action action, std::uint32_t nowMs) {
    if (!isDirection(action)) {
        return;
    }
    held_ = action;
    nextMs_ = nowMs + kDelayMs;
}

void KeyRepeat::release(Action action) {
    if (held_ == action) {
        held_.reset();
    }
}

std::optional<Action> KeyRepeat::poll(std::uint32_t nowMs) {
    if (!held_ || nowMs < nextMs_) {
        return std::nullopt;
    }
    nextMs_ = nowMs + kIntervalMs;
    return held_;
}

}  // namespace playfin::platform
