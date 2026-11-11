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

const char* primaryFamily(std::string_view language, const ThemeFont& font) {
    const std::string_view base = language.substr(0, language.find('-'));
    if (((base == "ru" || base == "uk") && !font.cyrillic) || (base == "vi" && !font.vietnamese)) {
        return "NotoSans";
    }
    return font.family;
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

bool NanoVgText::useFont(const std::string& fontDir, const std::string& language, const ThemeFont& font) {
    const std::string family = primaryFamily(language, font);
    const bool noto = family == "NotoSans";
    // Noto has no SemiBold: SemiBold uses Bold there
    const std::string files[3] = {noto ? "NotoSans-Regular.ttf" : font.files[0],
                                  noto ? "NotoSans-Bold.ttf" : font.files[1],
                                  noto ? "NotoSans-Bold.ttf" : font.files[2]};
    const char* suffixes[3] = {"-regular", "-semibold", "-bold"};
    if (nvgFindFont(vg_, (family + suffixes[0]).c_str()) < 0) {
        if (cjk_ == -2) {
            cjk_ = nvgCreateFontAtIndex(vg_, "cjk", (fontDir + "/NotoSansCJK-Regular.ttc").c_str(), cjkFaceFor(language));
            if (cjk_ < 0) {
                PF_LOG_WARN("no CJK font: Chinese, Japanese and Korean text will not show");
            }
        }
        if (!noto && noto_[0] == -2) {  // Cyrillic, Vietnamese marks, symbols
            noto_[0] = nvgCreateFont(vg_, "noto-regular", (fontDir + "/NotoSans-Regular.ttf").c_str());
            noto_[1] = nvgCreateFont(vg_, "noto-bold", (fontDir + "/NotoSans-Bold.ttf").c_str());
        }
        for (int i = 0; i < 3; ++i) {
            const int face = nvgCreateFont(vg_, (family + suffixes[i]).c_str(), (fontDir + "/" + files[i]).c_str());
            if (face < 0) {
                PF_LOG_ERROR("cannot load the font %s/%s", fontDir.c_str(), files[i].c_str());
                return false;
            }
            const int fallback = noto ? -1 : noto_[i == 0 ? 0 : 1];
            if (fallback >= 0) {
                nvgAddFallbackFontId(vg_, face, fallback);
            }
            if (cjk_ >= 0) {
                nvgAddFallbackFontId(vg_, face, cjk_);
            }
        }
    }
    setFontFamily(family);
    // where the capitals sit (the text drawing centres them in their lines)
    std::string regular;
    if (core::readSmallFile(fontDir + "/" + files[0], 32u << 20, regular) == core::ReadStatus::Ok) {
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
