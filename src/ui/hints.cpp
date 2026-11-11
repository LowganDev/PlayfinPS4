#include "ui/hints.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "ui/draw.hpp"
#include "ui/i18n.hpp"
#include "ui/sprites.hpp"
#include "ui/text.hpp"

namespace playfin::ui {

namespace {

using platform::Action;

constexpr float kGlyph = 30;
constexpr float kGlyphGap = 4;  // between the two glyphs of a pair

// the label of the innermost binding of `action` whose label may show now; nullopt when the button is not bound,
// "" when it is bound without a label (or with labels all hidden right now)
std::optional<std::string> labelOf(const std::vector<const ActionBinding*>& chain, Action action) {
    bool bound = false;
    for (const ActionBinding* binding : chain) {
        if (binding->action != action) {
            continue;
        }
        if (binding->shown && !binding->shown()) {
            bound = true;  // hidden for now: another binding of the button may show its own label
            continue;
        }
        return binding->label;
    }
    return bound ? std::optional<std::string>(std::string()) : std::nullopt;
}

// a pair (L1/R1, L2/R2, Up/Down, Left/Right): one entry when they share a label or one of them has none
void addPair(std::vector<HintItem>& items, const std::vector<const ActionBinding*>& chain, Action first,
             Action second) {
    const auto a = labelOf(chain, first);
    const auto b = labelOf(chain, second);
    const std::string la = a.value_or("");
    const std::string lb = b.value_or("");
    if (la.empty() && lb.empty()) {
        return;
    }
    if (la == lb || (la.empty() && a) || (lb.empty() && b)) {
        items.push_back({{first, second}, la.empty() ? lb : la});
        return;
    }
    if (!la.empty()) {
        items.push_back({{first}, la});
    }
    if (!lb.empty()) {
        items.push_back({{second}, lb});
    }
}

void addOne(std::vector<HintItem>& items, const std::vector<const ActionBinding*>& chain, Action action) {
    const auto label = labelOf(chain, action);
    if (label && !label->empty()) {
        items.push_back({{action}, *label});
    }
}

bool countFocusable(const View* view, int& count) {
    if (!view->visible) {
        return false;
    }
    if (view->canFocus() && ++count >= 2) {
        return true;
    }
    for (const auto& child : view->children()) {
        if (countFocusable(child.get(), count)) {
            return true;
        }
    }
    return false;
}

void arrow(NVGcontext* vg, float cx, float cy, float s, Action direction) {
    // a filled triangle pointing that way
    nvgBeginPath(vg);
    switch (direction) {
    case Action::Up:
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx + s, cy + s * 0.6f);
        nvgLineTo(vg, cx - s, cy + s * 0.6f);
        break;
    case Action::Down:
        nvgMoveTo(vg, cx, cy + s);
        nvgLineTo(vg, cx - s, cy - s * 0.6f);
        nvgLineTo(vg, cx + s, cy - s * 0.6f);
        break;
    case Action::Left:
        nvgMoveTo(vg, cx - s, cy);
        nvgLineTo(vg, cx + s * 0.6f, cy - s);
        nvgLineTo(vg, cx + s * 0.6f, cy + s);
        break;
    default:
        nvgMoveTo(vg, cx + s, cy);
        nvgLineTo(vg, cx - s * 0.6f, cy + s);
        nvgLineTo(vg, cx - s * 0.6f, cy - s);
        break;
    }
    nvgClosePath(vg);
    nvgFill(vg);
}

// the d-pad: a plus with the four arrows in it
void drawPad(NVGcontext* vg, float x, float cy) {
    const NVGcolor color = nvgColor(theme().textSecondary);
    const float r = kGlyph / 2;
    const float cx = x + r;
    const float arm = r * 0.42f;
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 2.5f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - arm, cy - r);
    nvgLineTo(vg, cx + arm, cy - r);
    nvgLineTo(vg, cx + arm, cy - arm);
    nvgLineTo(vg, cx + r, cy - arm);
    nvgLineTo(vg, cx + r, cy + arm);
    nvgLineTo(vg, cx + arm, cy + arm);
    nvgLineTo(vg, cx + arm, cy + r);
    nvgLineTo(vg, cx - arm, cy + r);
    nvgLineTo(vg, cx - arm, cy + arm);
    nvgLineTo(vg, cx - r, cy + arm);
    nvgLineTo(vg, cx - r, cy - arm);
    nvgLineTo(vg, cx - arm, cy - arm);
    nvgClosePath(vg);
    nvgStroke(vg);
}

// PlayStation face buttons drawn as outlines, directions as an arrow in a circle, the touchpad as a rounded
// rectangle; shoulder and option buttons as their name. Returns the width used.
float drawGlyph(NVGcontext* vg, Action action, float x, float cy) {
    const NVGcolor color = nvgColor(theme().textSecondary);
    const float r = kGlyph / 2;
    const float cx = x + r;
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, 2.5f);
    if (action == Action::Touchpad) {  // the pad itself: a wide rounded rectangle
        const float w = kGlyph * 1.5f;
        const float h = kGlyph * 0.8f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, cy - h / 2, w, h, 5);
        nvgStroke(vg);
        return w;
    }
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStroke(vg);
    nvgBeginPath(vg);
    const float s = r * 0.45f;
    switch (action) {
    case Action::Confirm:  // cross
        nvgMoveTo(vg, cx - s, cy - s);
        nvgLineTo(vg, cx + s, cy + s);
        nvgMoveTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx - s, cy + s);
        break;
    case Action::Back:  // circle
        nvgCircle(vg, cx, cy, s);
        break;
    case Action::Context:  // square
        nvgRect(vg, cx - s, cy - s, 2 * s, 2 * s);
        break;
    case Action::Options:  // triangle
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx + s, cy + s * 0.8f);
        nvgLineTo(vg, cx - s, cy + s * 0.8f);
        nvgClosePath(vg);
        break;
    case Action::Up:
    case Action::Down:
    case Action::Left:
    case Action::Right:
        arrow(vg, cx, cy, s, action);
        return kGlyph;
    default: {
        const char* text = platform::actionName(action);
        nvgFontFace(vg, fontFace(FontWeight::Bold));
        nvgFontSize(vg, 16);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, cx, cy + middleAlignedShift(16), text, nullptr);
        return kGlyph;
    }
    }
    nvgStroke(vg);
    return kGlyph;
}

bool isPad(const HintItem& item) {
    return item.glyphs.size() == 4 && item.glyphs[0] == Action::Up && item.glyphs[3] == Action::Right;
}

}  // namespace

std::vector<HintItem> hintItems(const std::vector<const ActionBinding*>& chain, bool navigable, bool canGoBack) {
    std::vector<HintItem> items;
    if (navigable) {
        items.push_back({{Action::Up, Action::Down, Action::Left, Action::Right}, tr("hints.navigate")});
    }
    addPair(items, chain, Action::Up, Action::Down);
    addPair(items, chain, Action::Left, Action::Right);
    addOne(items, chain, Action::Confirm);
    addOne(items, chain, Action::Options);
    addOne(items, chain, Action::Context);
    addPair(items, chain, Action::L1, Action::R1);
    addPair(items, chain, Action::L2, Action::R2);
    addOne(items, chain, Action::Menu);
    addOne(items, chain, Action::Touchpad);
    if (const auto back = labelOf(chain, Action::Back)) {  // the screen's own back, when it shows
        if (!back->empty()) {
            items.push_back({{Action::Back}, *back});
        }
    } else if (canGoBack) {
        items.push_back({{Action::Back}, tr("hints.back")});
    }
    // buttons that do the same (← → and L1 R1 in the player): one entry
    std::vector<HintItem> merged;
    for (HintItem& item : items) {
        auto same = std::find_if(merged.begin(), merged.end(), [&item](const HintItem& earlier) {
            return earlier.label == item.label && earlier.glyphs.size() < 4 && item.glyphs.size() < 4;
        });
        if (same == merged.end()) {
            merged.push_back(std::move(item));
        } else {
            same->glyphs.insert(same->glyphs.end(), item.glyphs.begin(), item.glyphs.end());
        }
    }
    return merged;
}

bool navigable(const View* scope) {
    if (scope == nullptr || !scope->navigationHint) {
        return false;
    }
    int count = 0;
    return countFocusable(scope, count);
}

void drawHints(NVGcontext* vg, const std::vector<HintItem>& items, float width, float height) {
    if (items.empty()) {
        return;
    }
    // a fade into the background under the bar: readable over posters and lists, no band. A sprite: as a gradient
    // it took half of a frame's time on the PS4 (the renderer's general shader)
    const int fade = 104;  // above the player's bar (its bottom at 100)
    drawVerticalFade(vg, 0, static_cast<float>(std::floor(height)) - static_cast<float>(fade), static_cast<int>(width), fade, fade - 66, theme().background,
                     0, 0xF2);
    const float cy = height - 44;
    float x = theme().marginSide;
    for (const HintItem& item : items) {
        if (isPad(item)) {
            drawPad(vg, x, cy);
            x += kGlyph;
        } else {
            for (std::size_t i = 0; i < item.glyphs.size(); ++i) {
                x += drawGlyph(vg, item.glyphs[i], x, cy);
                if (i + 1 < item.glyphs.size()) {
                    x += kGlyphGap;
                }
            }
        }
        x += theme().spacingS;
        float bounds[4];
        nvgFontFace(vg, fontFace(FontWeight::Regular));
        nvgFontSize(vg, theme().fontSmall);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgColor(theme().textSecondary));
        nvgText(vg, x, cy + middleAlignedShift(theme().fontSmall), item.label.c_str(), nullptr);
        x += nvgTextBounds(vg, 0, 0, item.label.c_str(), nullptr, bounds) + theme().spacingXl;
    }
}

}  // namespace playfin::ui
