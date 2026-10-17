#pragma once

#include <nanovg.h>

#include "ui/theme.hpp"

namespace playfin::ui {

inline NVGcolor nvgColor(Color color) { return nvgRGBA(color.r, color.g, color.b, color.a); }

}  // namespace playfin::ui
