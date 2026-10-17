#include <cstddef>  // orbis/_types/common_dialog.h uses size_t without including it

#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/log.hpp"
#include "core/utf.hpp"
#include "platform/text_input.hpp"

// The system keyboard works in UTF-16. OpenOrbis declares the buffers as wchar_t, which is 4 bytes with this
// toolchain but 2 bytes for the system: always pass char16_t buffers.
static_assert(sizeof(char16_t) == 2, "UTF-16 buffers");

namespace playfin::platform {

namespace {

constexpr std::size_t kMaxText = 2048;
char16_t gBuffer[kMaxText + 1];
std::u16string gTitle;
bool gModulesLoaded = false;
bool gRunning = false;

// sceImeDialog option bits (the OpenOrbis headers only have the raw `option` field). The firmware refuses the
// password option (0x80bc0030) unless the type is ORBIS_TYPE_BASIC_LATIN (docs/ps4-notes.md).
constexpr uint32_t kImeOptionNoAutoCapitalization = 0x02;
constexpr uint32_t kImeOptionPassword = 0x04;
constexpr uint32_t kImeOptionNoLearning = 0x20;

bool loadModules() {
    if (gModulesLoaded) {
        return true;
    }
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) {
        PF_LOG_ERROR("keyboard: cannot load the IME dialog module");
        return false;
    }
    // the internal loader returns an unsigned error code
    if (static_cast<int32_t>(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG)) < 0 ||
        sceCommonDialogInitialize() < 0) {
        PF_LOG_WARN("keyboard: common dialog initialisation failed");
    }
    // needed before sceUserServiceGetInitialUser; an "already initialised" error (SDL may have done it) is fine
    sceUserServiceInitialize(nullptr);
    gModulesLoaded = true;
    return true;
}

}  // namespace

bool systemKeyboardAvailable() { return true; }

bool openSystemKeyboard(const TextInputRequest& request) {
    if (gRunning || !loadModules()) {
        return false;
    }
    int32_t user = -1;
    if (const int32_t rc = sceUserServiceGetInitialUser(&user); rc < 0 || user < 0) {
        PF_LOG_ERROR("keyboard: no signed-in user (0x%08x)", static_cast<unsigned>(rc));
        return false;
    }
    const std::size_t maxLength = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(1, request.maxLength)), 1, kMaxText);
    const std::u16string initial = core::utf8ToUtf16(request.initial);
    const std::size_t length = std::min(initial.size(), maxLength);
    std::copy(initial.begin(), initial.begin() + static_cast<std::ptrdiff_t>(length), gBuffer);
    gBuffer[length] = 0;
    gTitle = core::utf8ToUtf16(request.title);

    OrbisImeDialogSetting setting{};
    setting.userId = static_cast<uint32_t>(user);
    switch (request.kind) {
    case TextInputRequest::Kind::Url:
        setting.type = ORBIS_TYPE_TYPE_URL;
        break;
    case TextInputRequest::Kind::Number:
        setting.type = ORBIS_TYPE_NUMBER;
        break;
    case TextInputRequest::Kind::Password:
        setting.type = ORBIS_TYPE_BASIC_LATIN;  // required by the password option
        // masked, no suggestions, nothing remembered, no capital forced on the first letter
        setting.option = kImeOptionPassword | kImeOptionNoLearning | kImeOptionNoAutoCapitalization;
        break;
    default:
        setting.type = ORBIS_TYPE_DEFAULT;
        break;
    }
    setting.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    setting.maxTextLength = static_cast<uint32_t>(maxLength);
    setting.inputTextBuffer = reinterpret_cast<wchar_t*>(gBuffer);
    setting.title = reinterpret_cast<const wchar_t*>(gTitle.c_str());
    setting.posx = 1920 / 2;
    setting.posy = 1080 / 2;
    setting.horizontalAlignment = ORBIS_H_CENTER;
    setting.verticalAlignment = ORBIS_V_CENTER;
    const int32_t rc = sceImeDialogInit(&setting, nullptr);
    if (rc < 0) {
        PF_LOG_ERROR("keyboard: sceImeDialogInit failed (0x%08x)", static_cast<unsigned>(rc));
        return false;
    }
    gRunning = true;
    return true;
}

KeyboardStatus pollSystemKeyboard(std::string& text) {
    if (!gRunning) {
        return KeyboardStatus::Idle;
    }
    const OrbisDialogStatus status = sceImeDialogGetStatus();
    if (status == ORBIS_DIALOG_STATUS_RUNNING) {
        return KeyboardStatus::Running;
    }
    gRunning = false;
    if (status != ORBIS_DIALOG_STATUS_STOPPED) {
        sceImeDialogTerm();
        return KeyboardStatus::Failed;
    }
    OrbisDialogResult result{};
    const bool gotResult = static_cast<int32_t>(sceImeDialogGetResult(&result)) >= 0;
    sceImeDialogTerm();
    if (!gotResult) {
        return KeyboardStatus::Failed;
    }
    if (result.endstatus != ORBIS_DIALOG_OK) {
        return KeyboardStatus::Cancelled;
    }
    std::size_t length = 0;
    while (length < kMaxText && gBuffer[length] != 0) {
        ++length;
    }
    text = core::utf16ToUtf8(std::u16string_view(gBuffer, length));
    return KeyboardStatus::Done;
}

}  // namespace playfin::platform
