#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace playfin::core {

// Invalid input (bad bytes, overlong forms, encoded surrogates, unpaired surrogates) becomes U+FFFD.
std::u16string utf8ToUtf16(std::string_view utf8);
std::string utf16ToUtf8(std::u16string_view utf16);

// Decodes the code point at `pos` and advances `pos` past it (one byte on invalid input, giving U+FFFD).
char32_t nextCodePoint(std::string_view text, std::size_t& pos);
// Start of the code point that ends at `end` (0 when end is 0).
std::size_t previousCodePointStart(std::string_view text, std::size_t end);

}  // namespace playfin::core
