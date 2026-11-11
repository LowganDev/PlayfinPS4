#pragma once

#include <string>
#include <string_view>

#include "ui/images.hpp"
#include "ui/text.hpp"
#include "ui/theme.hpp"

struct NVGcontext;

namespace playfin::ui {

// Face of NotoSansCJK-Regular.ttc for the UI language: 0 JP, 1 KR, 2 SC, 3 TC (JP by default).
int cjkFaceFor(std::string_view language);

// Family of the UI text for a language: the theme font's, or "NotoSans" when it lacks the language's script
// (Cyrillic for ru and uk, Vietnamese marks for vi).
const char* primaryFamily(std::string_view language, const ThemeFont& font);

// The capital height of a TrueType / OpenType file as a fraction of its em (OS/2 sCapHeight), 0 when unknown.
float capHeightOf(const std::string& fontFile);

// Text metrics from NanoVG: faces "<family>-regular", "-semibold" and "-bold" of the current theme's family, each
// with Noto Sans and Noto Sans CJK as fallbacks.
class NanoVgText : public TextMetrics {
public:
    explicit NanoVgText(NVGcontext* vg) : vg_(vg) {}
    // Makes the theme's font the UI's (fontFace() names it): its three faces are loaded the first time, each with
    // Noto Sans and Noto Sans CJK as fallbacks, and kept for later switches. False when a file cannot be loaded.
    bool useFont(const std::string& fontDir, const std::string& language, const ThemeFont& font);
    float measure(std::string_view text, const FontStyle& style) override;
    float lineHeight(const FontStyle& style) override;

private:
    void apply(const FontStyle& style);
    NVGcontext* vg_;
    int noto_[2] = {-2, -2};  // Noto Sans regular and bold as fallbacks (-2: not loaded yet)
    int cjk_ = -2;
};

class NvgTextures : public TextureFactory {
public:
    explicit NvgTextures(NVGcontext* vg) : vg_(vg) {}
    int create(const std::uint8_t* rgba, int width, int height) override;
    void destroy(int id) override;

private:
    NVGcontext* vg_;
};

}  // namespace playfin::ui
