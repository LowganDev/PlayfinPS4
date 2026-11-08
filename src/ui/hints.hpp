#pragma once

#include <string>
#include <vector>

#include "ui/view.hpp"

namespace playfin::ui {

// One entry of the hints bar: its controller glyphs (Up, Down, Left, Right together: the d-pad) and its label.
struct HintItem {
    std::vector<platform::Action> glyphs;
    std::string label;
    bool operator==(const HintItem& other) const { return glyphs == other.glyphs && label == other.label; }
};

// What the bar lists for the focus chain (innermost binding first): "Navigate" when `navigable`, the directions
// with a role of their own, ✕, △, □, the shoulder pairs (one entry when one of the pair is labelled), OPTIONS,
// then ◯: the screen's own back when it binds one, else "Back" when `canGoBack`. Labelled bindings only, and only
// while their `shown` answers true.
std::vector<HintItem> hintItems(const std::vector<const ActionBinding*>& chain, bool navigable, bool canGoBack);

// Two views of `scope` can take the focus, and the scope allows the "Navigate" hint.
bool navigable(const View* scope);

// Bottom-left bar, in the order of hintItems.
void drawHints(NVGcontext* vg, const std::vector<HintItem>& items, float width, float height);

}  // namespace playfin::ui
