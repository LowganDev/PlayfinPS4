#include "ui/hints.hpp"

#include <nanovg.h>

#include <algorithm>
#include <string>

#include "ui/draw.hpp"
#include "ui/text.hpp"

namespace playfin::ui {

namespace {

constexpr float kGlyph = 30;

// PlayStation face buttons drawn as outlines; shoulder and option buttons as text pills
float drawGlyph(NVGcontext* vg, platform::Action action, float x, float cy) {
    const NVGcolor color = nvgColor(theme().textSecondary);
    const float r = kGlyph / 2;
    const float cx = x + r;
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 2.5f);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStroke(vg);
    nvgBeginPath(vg);
    const float s = r * 0.45f;
    switch (action) {
    case platform::Action::Confirm:  // cross
        nvgMoveTo(vg, cx - s, cy - s);
        nvgLineTo(vg, cx + s, cy + s);
        nvgMoveTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx - s, cy + s);
        break;
    case platform::Action::Back:  // circle
        nvgCircle(vg, cx, cy, s);
        break;
    case platform::Action::Context:  // square
        nvgRect(vg, cx - s, cy - s, 2 * s, 2 * s);
        break;
    case platform::Action::Options:  // triangle
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx + s, cy + s * 0.8f);
        nvgLineTo(vg, cx - s, cy + s * 0.8f);
        nvgClosePath(vg);
        break;
    default: {
        const char* text = platform::actionName(action);
        nvgFontFace(vg, "bold");
        nvgFontSize(vg, 16);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, color);
        nvgText(vg, cx, cy + middleAlignedShift(16), text, nullptr);
        return kGlyph;
    }
    }
    nvgStroke(vg);
    return kGlyph;
}

}  // namespace

void drawHints(NVGcontext* vg, const std::vector<const ActionBinding*>& chain, float width, float height) {
    std::vector<const ActionBinding*> shown;
    for (const ActionBinding* binding : chain) {
        if (binding->label.empty()) {
            continue;
        }
        if (std::none_of(shown.begin(), shown.end(),
                         [&](const ActionBinding* b) { return b->action == binding->action; })) {
            shown.push_back(binding);
        }
    }
    const float cy = height - 44;
    float x = width - theme().marginSide;
    nvgFontSize(vg, theme().fontSmall);
    nvgFontFace(vg, "regular");
    for (const ActionBinding* binding : shown) {
        float bounds[4];
        nvgFontFace(vg, "regular");
        nvgFontSize(vg, theme().fontSmall);
        const float labelWidth = nvgTextBounds(vg, 0, 0, binding->label.c_str(), nullptr, bounds);
        x -= labelWidth;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgColor(theme().textSecondary));
        nvgText(vg, x, cy + middleAlignedShift(theme().fontSmall), binding->label.c_str(), nullptr);
        x -= theme().spacingS + kGlyph;
        drawGlyph(vg, binding->action, x, cy);
        x -= theme().spacingXl;
    }
}

}  // namespace playfin::ui
