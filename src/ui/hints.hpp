#pragma once

#include <vector>

#include "ui/view.hpp"

namespace playfin::ui {

// Bottom-right bar: one controller glyph + label per labelled action (innermost first, one per action).
void drawHints(NVGcontext* vg, const std::vector<const ActionBinding*>& chain, float width, float height);

}  // namespace playfin::ui
