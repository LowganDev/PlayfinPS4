#pragma once

#include <string>
#include <string_view>

namespace playfin::core {

// ASCII letters lowered; every other byte (UTF-8 included) kept.
std::string asciiLower(std::string_view text);

// Without the spaces, tabs and line ends at both ends.
std::string_view trimmed(std::string_view text);

}  // namespace playfin::core
