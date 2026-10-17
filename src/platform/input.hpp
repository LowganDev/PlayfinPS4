#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace playfin::platform {

// What the user asked for, independent of the device (controller or keyboard).
enum class Action { Up, Down, Left, Right, Confirm, Back, Options, Context, L1, R1, L2, R2, Menu };
constexpr int kActionCount = 13;

const char* actionName(Action action);
std::optional<Action> actionFromName(std::string_view name);
bool isDirection(Action action);

// Repeats the held direction (the last one pressed): first after kDelayMs, then every kIntervalMs. A late poll
// gives one repeat, never a burst.
class KeyRepeat {
public:
    static constexpr std::uint32_t kDelayMs = 400;
    static constexpr std::uint32_t kIntervalMs = 80;

    void press(Action action, std::uint32_t nowMs);
    void release(Action action);
    std::optional<Action> poll(std::uint32_t nowMs);

private:
    std::optional<Action> held_;
    std::uint32_t nextMs_ = 0;
};

}  // namespace playfin::platform
