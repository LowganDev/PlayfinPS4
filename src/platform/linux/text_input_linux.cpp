#include "platform/text_input.hpp"

namespace playfin::platform {

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const TextInputRequest& request) {
    (void)request;
    return false;
}
KeyboardStatus pollSystemKeyboard(std::string& text) {
    (void)text;
    return KeyboardStatus::Idle;
}

}  // namespace playfin::platform
