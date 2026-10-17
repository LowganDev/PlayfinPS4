#pragma once

#include <SDL.h>

#include <optional>
#include <string>

#include "platform/input.hpp"

namespace playfin::platform {

struct Event {
    enum class Type { ActionDown, ActionUp, Quit, Text, EditDelete };
    Type type = Type::Quit;
    Action action = Action::Confirm;  // ActionDown / ActionUp
    std::string text;                 // Text (UTF-8)
};

Event actionEvent(Event::Type type, Action action);

// SDL event -> Playfin event. Stateful only for the analog triggers (press past 16000, release under 8000).
// System key repeat is ignored: KeyRepeat does it the same way on every device.
class EventTranslator {
public:
    std::optional<Event> translate(const SDL_Event& event);

private:
    std::optional<Event> trigger(bool& down, Action action, int value);

    bool l2Down_ = false;
    bool r2Down_ = false;
    bool textMode_ = false;

public:
    // while a text field is edited on Linux: typing gives Text, Backspace gives EditDelete, letter keys no actions
    void setTextMode(bool on) { textMode_ = on; }
};

}  // namespace playfin::platform
