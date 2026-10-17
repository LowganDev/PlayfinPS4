#pragma once

#include <string>
#include <string_view>

namespace playfin::platform {

// PS4 system language id (sceSystemServiceParamGetInt LANG) -> BCP-47 tag; unknown ids give "en-US".
std::string languageFromPs4Id(int id);

// "fr_FR.UTF-8" -> "fr-FR"; "C", "POSIX" and empty give "en-US".
std::string languageFromPosix(std::string_view locale);

}  // namespace playfin::platform
