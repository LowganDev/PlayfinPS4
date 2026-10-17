#include "core/utf.hpp"

namespace playfin::core {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

char32_t nextCodePoint(std::string_view text, std::size_t& pos) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char first = byte(pos);
    std::size_t length = 0;
    char32_t cp = 0;
    char32_t minimum = 0;
    if (first < 0x80) {
        ++pos;
        return first;
    } else if ((first & 0xE0) == 0xC0) {
        length = 2;
        cp = first & 0x1F;
        minimum = 0x80;
    } else if ((first & 0xF0) == 0xE0) {
        length = 3;
        cp = first & 0x0F;
        minimum = 0x800;
    } else if ((first & 0xF8) == 0xF0) {
        length = 4;
        cp = first & 0x07;
        minimum = 0x10000;
    } else {
        ++pos;
        return kReplacement;
    }
    if (pos + length > text.size()) {
        ++pos;
        return kReplacement;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char next = byte(pos + i);
        if ((next & 0xC0) != 0x80) {
            ++pos;
            return kReplacement;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;
        return kReplacement;
    }
    pos += length;
    return cp;
}

std::size_t previousCodePointStart(std::string_view text, std::size_t end) {
    if (end == 0) {
        return 0;
    }
    std::size_t start = end - 1;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80 && end - start < 4) {
        --start;
    }
    return start;
}

std::u16string utf8ToUtf16(std::string_view utf8) {
    std::u16string out;
    out.reserve(utf8.size());
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        const char32_t cp = nextCodePoint(utf8, pos);
        if (cp >= 0x10000) {
            const char32_t v = cp - 0x10000;
            out += static_cast<char16_t>(0xD800 + (v >> 10));
            out += static_cast<char16_t>(0xDC00 + (v & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

std::string utf16ToUtf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size());
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        const char16_t unit = utf16[i];
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < utf16.size() && utf16[i + 1] >= 0xDC00 &&
            utf16[i + 1] <= 0xDFFF) {
            const char32_t cp = 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                                (static_cast<char32_t>(utf16[i + 1]) - 0xDC00);
            appendUtf8(out, cp);
            ++i;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            appendUtf8(out, kReplacement);
        } else {
            appendUtf8(out, unit);
        }
    }
    return out;
}

}  // namespace playfin::core
