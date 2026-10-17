#include "ui/animation.hpp"

#include <algorithm>

namespace playfin::ui {

namespace {

// y of the CSS cubic-bezier(x1, y1, x2, y2) at x = t: x(s) solved by bisection (monotonic for x1, x2 in [0, 1])
float cubicBezier(float x1, float y1, float x2, float y2, float t) {
    const auto curve = [](float a, float b, float s) {
        const float u = 1 - s;
        return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s;
    };
    float lo = 0;
    float hi = 1;
    for (int i = 0; i < 24; ++i) {
        const float mid = (lo + hi) / 2;
        (curve(x1, x2, mid) < t ? lo : hi) = mid;
    }
    return curve(y1, y2, (lo + hi) / 2);
}

}  // namespace

float ease(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseOutCubic: {
        const float u = 1 - t;
        return 1 - u * u * u;
    }
    case Easing::EaseInOutCubic:
        if (t < 0.5f) {
            return 4 * t * t * t;
        } else {
            const float u = -2 * t + 2;
            return 1 - u * u * u / 2;
        }
    case Easing::Smooth:
        return cubicBezier(0.2f, 0.8f, 0.2f, 1.0f, t);
    }
    return t;
}

void Animator::animate(float* target, float to, float durationMs, Easing easing, const void* owner,
                       float delayMs) {
    cancelTarget(target);
    if (durationMs <= 0 && delayMs <= 0) {
        *target = to;
        return;
    }
    tweens_.push_back(Tween{target, *target, to, durationMs, -delayMs, easing, owner});
}

void Animator::cancel(const void* owner) {
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                                 [owner](const Tween& tween) { return owner != nullptr && tween.owner == owner; }),
                  tweens_.end());
}

void Animator::cancelTarget(const float* target) {
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                                 [target](const Tween& tween) { return tween.target == target; }),
                  tweens_.end());
}

void Animator::tick(float dtMs) {
    for (Tween& tween : tweens_) {
        tween.elapsed += dtMs;
        const float t = tween.duration > 0 ? tween.elapsed / tween.duration : (tween.elapsed >= 0 ? 1.0f : 0.0f);
        *tween.target = tween.from + (tween.to - tween.from) * ease(tween.easing, t);
    }
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                                 [](const Tween& tween) { return tween.elapsed >= tween.duration; }),
                  tweens_.end());
}

}  // namespace playfin::ui
