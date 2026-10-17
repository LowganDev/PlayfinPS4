#include "ui/focus.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <optional>

namespace playfin::ui {

namespace {

constexpr float kEpsilon = 1.0f;

bool inDirection(const Rect& from, const Rect& to, Direction direction) {
    switch (direction) {
    case Direction::Up:
        return to.bottom() <= from.y + kEpsilon;
    case Direction::Down:
        return to.y >= from.bottom() - kEpsilon;
    case Direction::Left:
        return to.right() <= from.x + kEpsilon;
    case Direction::Right:
        return to.x >= from.right() - kEpsilon;
    }
    return false;
}

float intervalGap(float a0, float a1, float b0, float b1) { return std::max(0.0f, std::max(a0, b0) - std::min(a1, b1)); }

// float only: the PS4 libc++ has no float overloads of fabs/fmin (silent double conversions)
float absolute(float value) { return value < 0 ? -value : value; }

bool horizontal(Direction direction) { return direction == Direction::Left || direction == Direction::Right; }

void collect(View* view, View* focused, std::vector<View*>& out) {
    if (!view->visible) {
        return;
    }
    if (view != focused && view->canFocus() && !view->isAncestorOf(focused)) {
        out.push_back(view);
    }
    for (const auto& child : view->children()) {
        collect(child.get(), focused, out);
    }
}

std::optional<Direction> directionOf(platform::Action action) {
    switch (action) {
    case platform::Action::Up:
        return Direction::Up;
    case platform::Action::Down:
        return Direction::Down;
    case platform::Action::Left:
        return Direction::Left;
    case platform::Action::Right:
        return Direction::Right;
    default:
        return std::nullopt;
    }
}

}  // namespace

bool FocusManager::focus(View* view) {
    for (int guard = 0; view != nullptr && guard < 32; ++guard) {
        View* entry = view->focusEntry();
        if (entry == view) {
            break;
        }
        view = entry;
    }
    if (view == nullptr || !view->canFocus()) {
        return false;
    }
    if (view == focused_) {
        return true;
    }
    View* old = focused_;
    focused_ = view;
    if (old != nullptr) {
        old->onFocusChanged(false);
    }
    view->onFocusChanged(true);
    for (View* ancestor = view->parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        if (ancestor->rememberFocus) {
            ancestor->remembered = view;
        }
        ancestor->onDescendantFocused(view);
    }
    return true;
}

bool FocusManager::move(Direction direction) {
    if (focused_ == nullptr) {
        return scope_ != nullptr && focus(scope_);
    }
    for (View* ancestor = focused_; ancestor != nullptr; ancestor = ancestor->parent()) {
        // an explicit target is only followed while it still exists (it may have been rebuilt meanwhile)
        if (View* target = ancestor->nextFocus(direction); target != nullptr && scope_ != nullptr &&
                                                            scope_->containsPointer(target)) {
            return focus(target);
        }
        if (View* target = ancestor->navigate(focused_, direction)) {
            return focus(target);
        }
        if (ancestor == scope_) {
            break;
        }
    }
    if (scope_ == nullptr) {
        return false;
    }
    std::vector<View*> candidates;
    collect(scope_, focused_, candidates);
    const Rect from = focused_->absoluteFrame();
    View* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    float bestCenter = std::numeric_limits<float>::max();
    for (View* candidate : candidates) {
        const Rect to = candidate->absoluteFrame();
        if (!inDirection(from, to, direction)) {
            continue;
        }
        float primary = 0;
        float perpendicular = 0;
        float centerDistance = 0;
        if (horizontal(direction)) {
            primary = direction == Direction::Right ? to.x - from.right() : from.x - to.right();
            perpendicular = intervalGap(from.y, from.bottom(), to.y, to.bottom());
            if (perpendicular > 0) {
                continue;  // left/right stay on the same line
            }
            centerDistance = absolute(to.centerY() - from.centerY());
        } else {
            primary = direction == Direction::Down ? to.y - from.bottom() : from.y - to.bottom();
            perpendicular = intervalGap(from.x, from.right(), to.x, to.right());
            centerDistance = absolute(to.centerX() - from.centerX());
        }
        const float score = std::max(0.0f, primary) + 2 * perpendicular;
        if (score < bestScore || (score == bestScore && centerDistance < bestCenter)) {
            best = candidate;
            bestScore = score;
            bestCenter = centerDistance;
        }
    }
    if (best == nullptr) {
        return false;
    }
    View* target = best;
    for (View* ancestor = best->parent(); ancestor != nullptr && ancestor != scope_; ancestor = ancestor->parent()) {
        if (ancestor->rememberFocus && !ancestor->isAncestorOf(focused_)) {
            target = ancestor;
        }
    }
    return focus(target);
}

void FocusManager::forget(View* view) {
    if (focused_ == view) {
        focused_ = nullptr;
    }
    if (scope_ == view) {
        scope_ = nullptr;
    }
}

std::vector<const ActionBinding*> FocusManager::actionChain() const {
    std::vector<View*> path;
    for (View* view = focused_; view != nullptr; view = view->parent()) {
        path.push_back(view);
        if (view == scope_) {
            break;
        }
    }
    // a hidden view (or one inside a hidden view) that kept the focus does nothing
    std::size_t shown = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!path[i]->visible) {
            shown = i + 1;
        }
    }
    std::vector<const ActionBinding*> chain;
    for (std::size_t i = shown; i < path.size(); ++i) {
        for (const ActionBinding& binding : path[i]->bindings()) {
            chain.push_back(&binding);
        }
    }
    return chain;
}

bool FocusManager::dispatch(platform::Action action) {
    std::vector<std::function<bool()>> handlers;
    for (const ActionBinding* binding : actionChain()) {
        if (binding->action == action) {
            handlers.push_back(binding->handler);
        }
    }
    // copies: a handler may destroy the views that bound it
    for (auto& handler : handlers) {
        if (handler && handler()) {
            return true;
        }
    }
    if (const auto direction = directionOf(action)) {
        return move(*direction);
    }
    return false;
}

}  // namespace playfin::ui
