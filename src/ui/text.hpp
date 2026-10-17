#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace playfin::ui {

enum class FontWeight { Regular, SemiBold, Bold };

struct FontStyle {
    float size = 24;
    FontWeight weight = FontWeight::Regular;
};

// NanoVG face name of a weight: "regular", "semibold", "bold".
const char* fontFace(FontWeight weight);

// Where the loaded typeface's capitals sit, as fractions of the font size (fontstash's ascender and descender are
// fractions of ascender − descender while glyphs are scaled to the em: text drawn at the top of a line box sits
// high). Set by NanoVgText::loadFonts; unknown (no shift) otherwise.
struct TextGeometry {
    float capHeight = 0;  // em fraction
    float ascender = 0;   // fontstash's, size fraction
    float descender = 0;  // fontstash's, size fraction (negative)
    bool known = false;
};
void setTextGeometry(const TextGeometry& geometry);
// Text drawn top-aligned in a line box of `lineHeight`: how much lower to draw it so that its capitals sit in the
// middle of the box.
float topAlignedShift(float size, float lineHeight);
// Text drawn middle-aligned at y: how much lower to draw it so that the middle of its capitals is at y.
float middleAlignedShift(float size);

// Text measurement. NanoVgText (ui/nvg_backend) implements measure and lineHeight; tests use fixed metrics.
class TextMetrics {
public:
    virtual ~TextMetrics() = default;
    virtual float measure(std::string_view text, const FontStyle& style) = 0;
    virtual float lineHeight(const FontStyle& style) = 0;

    // Word-wrapped lines no wider than maxWidth ('\n' always breaks; words longer than a line break between
    // characters, which also handles CJK). With maxLines > 0, the last kept line ends with "…" when text was cut.
    // maxWidth <= 0 means no wrapping.
    std::vector<std::string> breakLines(std::string_view text, const FontStyle& style, float maxWidth,
                                        int maxLines = 0);
};

}  // namespace playfin::ui
