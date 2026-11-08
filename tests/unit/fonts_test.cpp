#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/utf.hpp"
#include "jellyfin/json.hpp"
#include "support/files.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace json = playfin::jellyfin::json;

namespace {

struct Face {
    std::string bytes;
    stbtt_fontinfo info{};
    bool ok = false;
    explicit Face(const std::string& file, int index = 0) : bytes(playfin::test::readFile(file)) {
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
        const int offset = stbtt_GetFontOffsetForIndex(data, index);
        ok = offset >= 0 && stbtt_InitFont(&info, data, offset) != 0;
    }
    bool has(char32_t c) const { return stbtt_FindGlyphIndex(&info, static_cast<int>(c)) != 0; }
};

}  // namespace

// The UI draws with Sora (Noto Sans for Russian, Ukrainian and Vietnamese), Noto Sans as the fallback and the CJK
// font last: a character none of them has shows as an empty box on the console.
TEST_CASE("every character of every translation is in the shipped fonts") {
    const std::string fonts = std::string(PLAYFIN_SOURCE_DIR) + "/resources/fonts/";
    std::vector<Face> faces;
    faces.emplace_back(fonts + "Sora-Regular.ttf");
    faces.emplace_back(fonts + "NotoSans-Regular.ttf");
    faces.emplace_back(fonts + "NotoSansCJK-Regular.ttc");
    for (const Face& face : faces) {
        REQUIRE(face.ok);
    }
    const std::string dir = std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n/";
    for (const char* language :
         {"en", "fr", "de", "es", "pt", "ru", "uk", "cs", "tr", "vi", "ja", "ko", "zh-Hans", "zh-Hant"}) {
        const auto doc = json::Document::parse(playfin::test::readFile(dir + language + ".json"));
        REQUIRE(doc);
        yyjson_val* key = nullptr;
        yyjson_obj_iter iter = yyjson_obj_iter_with(doc->root());
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (!yyjson_is_str(value)) {
                continue;
            }
            const std::string text = yyjson_get_str(value);
            for (std::size_t pos = 0; pos < text.size();) {
                const char32_t c = playfin::core::nextCodePoint(text, pos);
                if (c < 0x80 || c == '\n') {
                    continue;
                }
                bool found = false;
                for (const Face& face : faces) {
                    found = found || face.has(c);
                }
                if (!found) {
                    char code[16];
                    std::snprintf(code, sizeof(code), "U+%04X", static_cast<unsigned>(c));
                    const std::string message =
                        std::string(language) + " " + yyjson_get_str(key) + ": " + code + " is in no font";
                    FAIL_CHECK(message);
                }
            }
        }
    }
}
