#pragma once

#include <cstdint>
#include <string>

// Implemented once per target (platform/linux, platform/ps4); used only by sdl_platform.cpp.
namespace playfin::platform::backend {

void postInit();  // after the window and GL context exist
std::string defaultDataDir();
std::string systemLanguage();
int utcOffsetMinutes(std::int64_t utcSeconds);
std::string resourceDir();

}  // namespace playfin::platform::backend
