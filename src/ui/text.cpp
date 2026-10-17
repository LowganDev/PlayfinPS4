#include "ui/text.hpp"

#include "core/utf.hpp"

namespace playfin::ui {

const char* fontFace(FontWeight weight) {
    switch (weight) {
    case FontWeight::SemiBold:
        return "semibold";
    case FontWeight::Bold:
        return "bold";
    case FontWeight::Regular:
        break;
    }
    return "regular";
}


namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

std::string trimRight(std::string text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

}  // namespace

std::vector<std::string> TextMetrics::breakLines(std::string_view text, const FontStyle& style, float maxWidth,
                                                 int maxLines) {
    std::vector<std::string> lines;
    const bool wrap = maxWidth > 0;
    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        std::size_t paragraphEnd = text.find('\n', paragraphStart);
        if (paragraphEnd == std::string_view::npos) {
            paragraphEnd = text.size();
        }
        const std::string_view paragraph = text.substr(paragraphStart, paragraphEnd - paragraphStart);
        paragraphStart = paragraphEnd + 1;
        if (!wrap) {
            lines.emplace_back(paragraph);
            continue;
        }
        std::string current;
        std::size_t wordStart = 0;
        while (wordStart <= paragraph.size()) {
            std::size_t wordEnd = paragraph.find(' ', wordStart);
            if (wordEnd == std::string_view::npos) {
                wordEnd = paragraph.size();
            }
            const std::string_view word = paragraph.substr(wordStart, wordEnd - wordStart);
            wordStart = wordEnd + 1;
            if (word.empty()) {
                continue;
            }
            const std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
            if (measure(candidate, style) <= maxWidth) {
                current = candidate;
                continue;
            }
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }
            if (measure(word, style) <= maxWidth) {
                current = std::string(word);
                continue;
            }
            // break the word between characters
            std::size_t pos = 0;
            while (pos < word.size()) {
                const std::size_t start = pos;
                core::nextCodePoint(word, pos);
                const std::string attempt = current + std::string(word.substr(start, pos - start));
                if (!current.empty() && measure(attempt, style) > maxWidth) {
                    lines.push_back(current);
                    current = std::string(word.substr(start, pos - start));
                } else {
                    current = attempt;
                }
            }
        }
        lines.push_back(current);
    }
    if (maxLines > 0 && lines.size() > static_cast<std::size_t>(maxLines)) {
        lines.resize(static_cast<std::size_t>(maxLines));
        std::string last = trimRight(lines.back());
        while (!last.empty() && wrap && measure(last + std::string(kEllipsis), style) > maxWidth) {
            last = trimRight(last.substr(0, core::previousCodePointStart(last, last.size())));
        }
        lines.back() = last + std::string(kEllipsis);
    }
    return lines;
}

namespace {
TextGeometry gGeometry;
}  // namespace

void setTextGeometry(const TextGeometry& geometry) { gGeometry = geometry; }

float topAlignedShift(float size, float lineHeight) {
    if (!gGeometry.known) {
        return 0;
    }
    return lineHeight / 2 + gGeometry.capHeight * size / 2 - gGeometry.ascender * size;
}

float middleAlignedShift(float size) {
    if (!gGeometry.known) {
        return 0;
    }
    return (gGeometry.capHeight / 2 - (gGeometry.ascender + gGeometry.descender) / 2) * size;
}

}  // namespace playfin::ui
