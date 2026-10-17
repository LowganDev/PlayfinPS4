#include <unistd.h>

#include <cstdlib>
#include <ctime>

#include "platform/backend.hpp"
#include "platform/language.hpp"

namespace playfin::platform::backend {


void postInit() {}

std::string defaultDataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/playfin";
    }
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr && *home != '\0' ? home : "/tmp") + "/.local/share/playfin";
}

int utcOffsetMinutes(std::int64_t utcSeconds) {
    const std::time_t t = static_cast<std::time_t>(utcSeconds);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) {
        return 0;
    }
    return static_cast<int>(local.tm_gmtoff / 60);
}

std::string systemLanguage() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0') {
            return languageFromPosix(value);
        }
    }
    return "en-US";
}

std::string resourceDir() {
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return "resources";
    }
    const std::string exe(buffer, static_cast<std::size_t>(length));
    return exe.substr(0, exe.rfind('/')) + "/resources";
}

}  // namespace playfin::platform::backend
