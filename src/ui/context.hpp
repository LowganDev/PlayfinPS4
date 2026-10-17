#pragma once

namespace playfin::ui {

class Animator;
class FocusManager;
class ImageLoader;
class TextMetrics;

// Services of the running UI, set by Application (or by tests). Views use them for animations, focus and images.
struct Context {
    Animator* animator = nullptr;
    FocusManager* focus = nullptr;
    ImageLoader* images = nullptr;
    TextMetrics* metrics = nullptr;
    float timeMs = 0;  // frame clock (fixed steps in test mode)
    bool redrawNextFrame = false;  // set while drawing by views that change every frame (spinner)
};

Context& context();

}  // namespace playfin::ui
