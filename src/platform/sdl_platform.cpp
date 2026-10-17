#include <SDL.h>

#include <algorithm>
#include <cerrno>
#include <vector>

#include "core/boot_trace.hpp"
#include "core/data_dir.hpp"
#include "core/log.hpp"
#include "platform/backend.hpp"
#include "platform/platform.hpp"
#include "platform/text_input.hpp"

namespace playfin::platform {

namespace {

SDL_Window* gWindow = nullptr;
SDL_GLContext gContext = nullptr;
EventTranslator gTranslator;
KeyRepeat gRepeat;
std::vector<SDL_GameController*> gControllers;

void openController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) {
        PF_LOG_WARN("input device %d is not a game controller", deviceIndex);
        return;
    }
    SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == nullptr) {
        PF_LOG_WARN("cannot open controller %d: %s", deviceIndex, SDL_GetError());
        return;
    }
    PF_LOG_INFO("controller connected: %s", SDL_GameControllerName(controller));
    gControllers.push_back(controller);
}

void closeController(SDL_JoystickID instance) {
    auto it = std::find_if(gControllers.begin(), gControllers.end(), [instance](SDL_GameController* c) {
        return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == instance;
    });
    if (it != gControllers.end()) {
        PF_LOG_INFO("controller disconnected");
        SDL_GameControllerClose(*it);
        gControllers.erase(it);
    }
}

}  // namespace

bool init(const Config& config) {
    // the log comes first: any later failure, SDL included, must reach the file (the only log readable on the PS4)
    const std::string dataDir = config.dataDirOverride.empty() ? backend::defaultDataDir() : config.dataDirOverride;
    const bool dataDirOk = core::makeDirs(dataDir);
    core::bootMark("data folder ok", dataDirOk ? 1 : 0);
    if (dataDirOk) {
        core::setDataRoot(dataDir);
        if (const auto logPath = core::dataPath("playfin.log"); !logPath || !log::openFile(*logPath)) {
            PF_LOG_WARN("cannot open the log file, logging to stderr only");
        }
    } else {
        // no data root: dataPath() gives nothing, so nothing is written anywhere; the app still runs
        PF_LOG_ERROR("cannot create the data directory %s (errno %d): nothing will be saved", dataDir.c_str(), errno);
    }
    PF_LOG_INFO("startup: SDL");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        PF_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    PF_LOG_INFO("startup: window");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = SDL_WINDOW_OPENGL | (config.hidden ? SDL_WINDOW_HIDDEN : 0u);
    gWindow = SDL_CreateWindow("Playfin", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, config.width,
                               config.height, flags);
    if (gWindow == nullptr) {
        PF_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    PF_LOG_INFO("startup: OpenGL ES context");
    gContext = SDL_GL_CreateContext(gWindow);
    if (gContext == nullptr) {
        PF_LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(gWindow, gContext);
    SDL_GL_SetSwapInterval(1);
    SDL_StopTextInput();  // desktop SDL starts with text input on
    backend::postInit();
    return true;
}

void shutdown() {
    for (SDL_GameController* controller : gControllers) {
        SDL_GameControllerClose(controller);
    }
    gControllers.clear();
    if (gContext != nullptr) {
        SDL_GL_DeleteContext(gContext);
        gContext = nullptr;
    }
    if (gWindow != nullptr) {
        SDL_DestroyWindow(gWindow);
        gWindow = nullptr;
    }
    SDL_Quit();
}

bool pollEvent(Event& out, std::uint32_t nowMs) {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            openController(event.cdevice.which);
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            closeController(event.cdevice.which);
        }
        const auto translated = gTranslator.translate(event);
        if (!translated) {
            continue;
        }
        if (translated->type == Event::Type::ActionDown) {
            gRepeat.press(translated->action, nowMs);
        } else if (translated->type == Event::Type::ActionUp) {
            gRepeat.release(translated->action);
        }
        out = *translated;
        return true;
    }
    if (const auto repeated = gRepeat.poll(nowMs)) {
        out = actionEvent(Event::Type::ActionDown, *repeated);
        return true;
    }
    return false;
}

void swap() { SDL_GL_SwapWindow(gWindow); }

Size drawableSize() {
    Size size;
    SDL_GL_GetDrawableSize(gWindow, &size.width, &size.height);
    return size;
}

std::uint32_t ticksMs() { return SDL_GetTicks(); }

void setTextEvents(bool on) {
    gTranslator.setTextMode(on);
    if (on) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

std::string systemLanguage() { return backend::systemLanguage(); }
int utcOffsetMinutes(std::int64_t utcSeconds) { return backend::utcOffsetMinutes(utcSeconds); }

std::string resourceDir() { return backend::resourceDir(); }

}  // namespace playfin::platform
