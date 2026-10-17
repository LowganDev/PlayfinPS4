#pragma once

#include <cstddef>
#include <vector>

namespace playfin::ui {

// Smooth: cubic-bezier(.2, .8, .2, 1), the identity's motion curve
enum class Easing { Linear, EaseOutCubic, EaseInOutCubic, Smooth };

// t is clamped to [0, 1]
float ease(Easing easing, float t);

// Float tweens ticked with the frame time (fixed in test mode). One tween per target: animating a target again
// restarts from its current value. Views cancel their tweens when destroyed (owner).
class Animator {
public:
    // After `delayMs` the target moves from its value at this call to `to`.
    void animate(float* target, float to, float durationMs, Easing easing, const void* owner, float delayMs = 0);
    void cancel(const void* owner);
    void cancelTarget(const float* target);
    void tick(float dtMs);
    bool active() const { return !tweens_.empty(); }
    std::size_t count() const { return tweens_.size(); }

private:
    struct Tween {
        float* target;
        float from;
        float to;
        float duration;
        float elapsed;  // negative during the delay
        Easing easing;
        const void* owner;
    };
    std::vector<Tween> tweens_;
};

}  // namespace playfin::ui
