#include "ui/reveal.hpp"

#include "ui/application.hpp"
#include "ui/theme.hpp"

namespace playfin::ui {

namespace {
constexpr std::size_t kMaxRevealed = 12;
}

void revealChildren(View& parent, std::size_t first) {
    Application* app = Application::current();
    if (app == nullptr || !app->transitions()) {
        return;
    }
    const auto& children = parent.children();
    for (std::size_t i = first; i < children.size() && i < first + kMaxRevealed; ++i) {
        View* child = children[i].get();
        const float delay = theme().revealGapMs * static_cast<float>(i - first);
        child->opacity = 0;
        child->shiftY = theme().revealRise;
        app->animator().animate(&child->opacity, 1, theme().revealMs, Easing::Smooth, child, delay);
        app->animator().animate(&child->shiftY, 0, theme().revealMs, Easing::Smooth, child, delay);
    }
}

}  // namespace playfin::ui
