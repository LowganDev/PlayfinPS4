#include "platform/remote.hpp"

namespace playfin::platform {

namespace {

struct Mapping {
    std::uint32_t bit;
    Action action;
};

// as the controller's buttons (events.cpp): cross confirms, circle goes back, triangle is Options, square Context,
// the OPTIONS button the menu
constexpr Mapping kMappings[] = {
    {RemoteButtons::kUp, Action::Up},           {RemoteButtons::kDown, Action::Down},
    {RemoteButtons::kLeft, Action::Left},       {RemoteButtons::kRight, Action::Right},
    {RemoteButtons::kCross, Action::Confirm},   {RemoteButtons::kCircle, Action::Back},
    {RemoteButtons::kTriangle, Action::Options}, {RemoteButtons::kSquare, Action::Context},
    {RemoteButtons::kL1, Action::L1},           {RemoteButtons::kR1, Action::R1},
    {RemoteButtons::kL2, Action::L2},           {RemoteButtons::kR2, Action::R2},
    {RemoteButtons::kOptions, Action::Menu},
};

}  // namespace

std::vector<Event> RemoteButtons::update(std::uint32_t buttons, std::uint8_t key) {
    switch (key) {  // a key is the button whose action it has
    case kKeyOk:
    case kKeyPlay:
    case kKeyPause:
    case kKeyPlayPause:
        buttons |= kCross;
        break;
    case kKeyBack:
        buttons |= kCircle;
        break;
    case kKeyMenu:
    case kKeyContextMenu:
        buttons |= kTriangle;
        break;
    case kKeyRewind:
        buttons |= kL1;
        break;
    case kKeyFastForward:
        buttons |= kR1;
        break;
    default:
        break;
    }
    std::vector<Event> events;
    for (const Mapping& mapping : kMappings) {
        const bool was = (held_ & mapping.bit) != 0;
        const bool is = (buttons & mapping.bit) != 0;
        if (is != was) {
            events.push_back(actionEvent(is ? Event::Type::ActionDown : Event::Type::ActionUp, mapping.action));
        }
    }
    held_ = buttons;
    return events;
}

}  // namespace playfin::platform
