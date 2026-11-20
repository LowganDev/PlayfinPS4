#pragma once

#include <cstdint>
#include <vector>

#include "platform/events.hpp"

namespace playfin::platform {

// The TV's remote control through HDMI Device Link (CEC). The console gives its keys as a pad of its own (the
// remote-control port of the pad library, see ps4/remote_ps4.cpp): the arrows as the controller's direction bits,
// every other key as a code in one byte of the sample (0 when none is held). This turns successive samples into
// the same actions as the controller's buttons.
class RemoteButtons {
public:
    // the pad library's button bits
    static constexpr std::uint32_t kOptions = 0x0008;
    static constexpr std::uint32_t kUp = 0x0010;
    static constexpr std::uint32_t kRight = 0x0020;
    static constexpr std::uint32_t kDown = 0x0040;
    static constexpr std::uint32_t kLeft = 0x0080;
    static constexpr std::uint32_t kL2 = 0x0100;
    static constexpr std::uint32_t kR2 = 0x0200;
    static constexpr std::uint32_t kL1 = 0x0400;
    static constexpr std::uint32_t kR1 = 0x0800;
    static constexpr std::uint32_t kTriangle = 0x1000;
    static constexpr std::uint32_t kCircle = 0x2000;
    static constexpr std::uint32_t kCross = 0x4000;
    static constexpr std::uint32_t kSquare = 0x8000;

    // The remote's key codes. Seen on a PS4 (2026-11-20, a TV's remote): OK and Back. The others are the codes
    // PS5 homebrew documents for the same port, not seen on a PS4 yet.
    static constexpr std::uint8_t kKeyOk = 13;
    static constexpr std::uint8_t kKeyBack = 15;
    static constexpr std::uint8_t kKeyMenu = 18;
    static constexpr std::uint8_t kKeyPlay = 22;
    static constexpr std::uint8_t kKeyRewind = 23;
    static constexpr std::uint8_t kKeyFastForward = 24;
    static constexpr std::uint8_t kKeyPause = 26;
    static constexpr std::uint8_t kKeyContextMenu = 27;
    static constexpr std::uint8_t kKeyPlayPause = 45;

    // The buttons and the key held now: an ActionDown for each action newly held, an ActionUp for each released.
    // OK and the play keys confirm (in the player: pause and play), Back goes back, the menu keys are Options,
    // rewind and fast forward are L1 and R1 (in the player: back and on).
    std::vector<Event> update(std::uint32_t buttons, std::uint8_t key = 0);
    // The remote went away: everything held is released.
    std::vector<Event> release() { return update(0, 0); }

private:
    std::uint32_t held_ = 0;
};

namespace backend {

// The remote control's buttons and key now; false when there is no remote (not a console, the port did not open,
// no data).
bool readRemote(std::uint32_t& buttons, std::uint8_t& key);

}  // namespace backend

}  // namespace playfin::platform
