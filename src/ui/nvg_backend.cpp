#include "ui/nvg_backend.hpp"

#include <nanovg.h>

#include <algorithm>

#include "core/file_io.hpp"
#include "core/log.hpp"

namespace playfin::ui {

int cjkFaceFor(std::string_view language) {
    if (language.rfind("ko", 0) == 0) {
        return 1;
    }
    if (language == "zh-Hant" || language == "zh-TW" || language == "zh-HK" || language == "zh-MO") {
        return 3;
    }
    if (language.rfind("zh", 0) == 0) {
        return 2;
    }
    return 0;
}

const char* primaryFamily(std::string_view language) {
    const std::string_view base = language.substr(0, language.find('-'));
    return base == "ru" || base == "uk" || base == "vi" ? "NotoSans" : "Sora";
}

namespace {

std::uint32_t read16(const std::string& data, std::size_t at) {
    return at + 2 <= data.size() ? (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at])) << 8) |
                                       static_cast<unsigned char>(data[at + 1])
                                 : 0;
}

std::uint32_t read32(const std::string& data, std::size_t at) { return (read16(data, at) << 16) | read16(data, at + 2); }

// the offset of a table of a TrueType / OpenType file, 0 when it has none
std::size_t tableOffset(const std::string& data, const char* tag) {
    const std::uint32_t count = read16(data, 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t record = 12 + static_cast<std::size_t>(i) * 16;
        if (record + 16 <= data.size() && data.compare(record, 4, tag) == 0) {
            return read32(data, record + 8);
        }
    }
    return 0;
}

}  // namespace

float capHeightOf(const std::string& fontFile) {
    const std::size_t head = tableOffset(fontFile, "head");
    const std::size_t os2 = tableOffset(fontFile, "OS/2");
    const std::uint32_t unitsPerEm = head != 0 ? read16(fontFile, head + 18) : 0;
    // OS/2 version 2 and later: sCapHeight at 88
    if (unitsPerEm == 0 || os2 == 0 || read16(fontFile, os2) < 2) {
        return 0;
    }
    return static_cast<float>(read16(fontFile, os2 + 88)) / static_cast<float>(unitsPerEm);
}

bool NanoVgText::loadFonts(const std::string& fontDir, const std::string& language) {
    const bool sora = std::string_view(primaryFamily(language)) == "Sora";
    // Noto has no SemiBold: SemiBold uses Bold there
    const char* files[3] = {sora ? "/Sora-Regular.ttf" : "/NotoSans-Regular.ttf",
                            sora ? "/Sora-SemiBold.ttf" : "/NotoSans-Bold.ttf",
                            sora ? "/Sora-Bold.ttf" : "/NotoSans-Bold.ttf"};
    const FontWeight weights[3] = {FontWeight::Regular, FontWeight::SemiBold, FontWeight::Bold};
    int faces[3];
    for (int i = 0; i < 3; ++i) {
        faces[i] = nvgCreateFont(vg_, fontFace(weights[i]), (fontDir + files[i]).c_str());
        if (faces[i] < 0) {
            PF_LOG_ERROR("cannot load the font %s%s", fontDir.c_str(), files[i]);
            return false;
        }
    }
    // where the capitals sit (the text drawing centres them in their lines)
    std::string regular;
    if (core::readSmallFile(fontDir + files[0], 32u << 20, regular) == core::ReadStatus::Ok) {
        TextGeometry geometry;
        geometry.capHeight = capHeightOf(regular);
        nvgFontFace(vg_, fontFace(FontWeight::Regular));
        nvgFontSize(vg_, 100);
        float ascender = 0;
        float descender = 0;
        nvgTextMetrics(vg_, &ascender, &descender, nullptr);
        geometry.ascender = ascender / 100;
        geometry.descender = descender / 100;
        geometry.known = geometry.capHeight > 0;
        setTextGeometry(geometry);
    }
    const int notoRegular = sora ? nvgCreateFont(vg_, "noto-regular", (fontDir + "/NotoSans-Regular.ttf").c_str()) : -1;
    const int notoBold = sora ? nvgCreateFont(vg_, "noto-bold", (fontDir + "/NotoSans-Bold.ttf").c_str()) : -1;
    const int cjk = nvgCreateFontAtIndex(vg_, "cjk", (fontDir + "/NotoSansCJK-Regular.ttc").c_str(),
                                         cjkFaceFor(language));
    if (cjk < 0) {
        PF_LOG_WARN("no CJK font: Chinese, Japanese and Korean text will not show");
    }
    for (int i = 0; i < 3; ++i) {
        const int noto = i == 0 ? notoRegular : notoBold;  // Cyrillic, Vietnamese marks, symbols
        if (noto >= 0) {
            nvgAddFallbackFontId(vg_, faces[i], noto);
        }
        if (cjk >= 0) {
            nvgAddFallbackFontId(vg_, faces[i], cjk);
        }
    }
    return true;
}

void NanoVgText::apply(const FontStyle& style) {
    nvgFontFace(vg_, fontFace(style.weight));
    nvgFontSize(vg_, style.size);
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
}

float NanoVgText::measure(std::string_view text, const FontStyle& style) {
    apply(style);
    return nvgTextBounds(vg_, 0, 0, text.data(), text.data() + text.size(), nullptr);
}

float NanoVgText::lineHeight(const FontStyle& style) {
    apply(style);
    float height = 0;
    nvgTextMetrics(vg_, nullptr, nullptr, &height);
    // Noto reports about 1.0 x size: too tight for paragraphs read from a couch
    return std::max(height, style.size * 1.3f);
}

int NvgTextures::create(const std::uint8_t* rgba, int width, int height) {
    return nvgCreateImageRGBA(vg_, width, height, 0, rgba);
}

void NvgTextures::destroy(int id) { nvgDeleteImage(vg_, id); }

}  // namespace playfin::ui
