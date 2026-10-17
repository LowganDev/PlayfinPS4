#pragma once

#include <string>

namespace playfin::platform {

struct TextInputRequest {
    std::string title;
    std::string initial;
    enum class Kind { Text, Url, Number, Password } kind = Kind::Text;
    int maxLength = 256;
};

enum class KeyboardStatus { Idle, Running, Done, Cancelled, Failed };

// PS4: the system keyboard dialog. Linux: not available (the UI edits with the PC keyboard).
bool systemKeyboardAvailable();
bool openSystemKeyboard(const TextInputRequest& request);
// Polled every frame while Running; Done fills `text` (UTF-8).
KeyboardStatus pollSystemKeyboard(std::string& text);

// Linux: SDL text events on/off (EventTranslator text mode). No effect on PS4.
void setTextEvents(bool on);

}  // namespace playfin::platform
