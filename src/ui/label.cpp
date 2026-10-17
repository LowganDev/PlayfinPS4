#include "ui/label.hpp"

#include <nanovg.h>

#include <algorithm>

#include "ui/draw.hpp"

namespace playfin::ui {

Label::Label(std::string text, FontStyle styleValue) : style(styleValue), color(theme().text), text_(std::move(text)) {}

void Label::setText(std::string text) {
    if (text == text_) {
        return;
    }
    text_ = std::move(text);
    linesWidth_ = -1;
    markDirty();
}

Size Label::measure(Size available, TextMetrics& metrics) {
    const float maxWidth = std::max(0.0f, available.width - padding.horizontal());
    const auto lines = metrics.breakLines(text_, style, maxWidth, maxLines);
    float widest = 0;
    for (const auto& line : lines) {
        widest = std::max(widest, metrics.measure(line, style));
    }
    return {widest + padding.horizontal(),
            metrics.lineHeight(style) * static_cast<float>(lines.size()) + padding.vertical()};
}

void Label::layout(TextMetrics& metrics) {
    const float width = std::max(0.0f, frame.width - padding.horizontal());
    if (width != linesWidth_ || needsLayout()) {
        lines_ = metrics.breakLines(text_, style, width, maxLines);
        linesWidth_ = width;
    }
    lineHeight_ = metrics.lineHeight(style);
    shift_ = topAlignedShift(style.size, lineHeight_);
}

void Label::draw(NVGcontext* vg) const {
    drawBackground(vg);
    nvgFontFace(vg, fontFace(style.weight));
    nvgFontSize(vg, style.size);
    nvgFillColor(vg, nvgColor(color));
    int alignFlags = NVG_ALIGN_TOP;
    float x = padding.left;
    if (align == TextAlign::Center) {
        alignFlags |= NVG_ALIGN_CENTER;
        x = padding.left + (frame.width - padding.horizontal()) / 2;
    } else if (align == TextAlign::Right) {
        alignFlags |= NVG_ALIGN_RIGHT;
        x = frame.width - padding.right;
    } else {
        alignFlags |= NVG_ALIGN_LEFT;
    }
    nvgTextAlign(vg, alignFlags);
    float y = padding.top + shift_;
    for (const auto& line : lines_) {
        nvgText(vg, x, y, line.c_str(), line.c_str() + line.size());
        y += lineHeight_;
    }
}

}  // namespace playfin::ui
