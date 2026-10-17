#pragma once

#include <string>
#include <string_view>

#include "ui/images.hpp"
#include "ui/text.hpp"

struct NVGcontext;

namespace playfin::ui {

// Face of NotoSansCJK-Regular.ttc for the UI language: 0 JP, 1 KR, 2 SC, 3 TC (JP by default).
int cjkFaceFor(std::string_view language);

// Family of the UI text for a language: "Sora", or "NotoSans" for languages whose script Sora lacks (ru, uk, vi).
const char* primaryFamily(std::string_view language);

// The capital height of a TrueType / OpenType file as a fraction of its em (OS/2 sCapHeight), 0 when unknown.
float capHeightOf(const std::string& fontFile);

// Text metrics from NanoVG: faces "regular", "semibold" and "bold" of the primary family, each with Noto Sans and
// Noto Sans CJK as fallbacks.
class NanoVgText : public TextMetrics {
public:
    explicit NanoVgText(NVGcontext* vg) : vg_(vg) {}
    bool loadFonts(const std::string& fontDir, const std::string& language);
    float measure(std::string_view text, const FontStyle& style) override;
    float lineHeight(const FontStyle& style) override;

private:
    void apply(const FontStyle& style);
    NVGcontext* vg_;
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
