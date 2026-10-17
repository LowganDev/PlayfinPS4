#include "ui/scroll.hpp"

#include <algorithm>

#include "ui/animation.hpp"
#include "ui/context.hpp"

namespace playfin::ui {

VScroll::VScroll() {
    clip = true;
    width = SizeSpec::fill();
    height = SizeSpec::fill();
}

void VScroll::setContent(std::unique_ptr<View> content) {
    clearChildren();
    addChild(std::move(content));
}

View* VScroll::content() const { return children().empty() ? nullptr : children().front().get(); }

Size VScroll::measure(Size available, TextMetrics& metrics) {
    View* view = content();
    if (maxHeight <= 0 || view == nullptr) {
        return available;
    }
    const float innerWidth = std::max(0.0f, available.width - padding.horizontal());
    const Size size = resolveSize(*view, {innerWidth, 1.0e6f}, metrics);
    return {available.width, std::min(maxHeight, size.height + view->margin.vertical() + padding.vertical())};
}

void VScroll::layout(TextMetrics& metrics) {
    View* view = content();
    if (view == nullptr) {
        return;
    }
    const float innerWidth = std::max(0.0f, frame.width - padding.horizontal());
    const Size size = resolveSize(*view, {innerWidth, 1.0e6f}, metrics);
    contentHeight_ = size.height + view->margin.vertical();
    view->place({padding.left + view->margin.left, padding.top + view->margin.top, innerWidth - view->margin.horizontal(),
                 size.height},
                metrics);
    offset_ = std::clamp(offset_, 0.0f, maxOffset());
}

float VScroll::maxOffset() const {
    return std::max(0.0f, contentHeight_ + padding.vertical() - frame.height);
}

void VScroll::scrollTo(float target) {
    target = std::clamp(target, 0.0f, maxOffset());
    if (Animator* animator = context().animator) {
        animator->animate(&offset_, target, theme().scrollMs, Easing::EaseOutCubic, this);
    } else {
        offset_ = target;
    }
}

void VScroll::onDescendantFocused(View* view) {
    View* body = content();
    if (body == nullptr || view == nullptr || !body->isAncestorOf(view)) {
        return;
    }
    if (policy == Policy::Leading) {
        View* row = view;
        while (row->parent() != nullptr && row->parent() != body) {
            row = row->parent();
        }
        if (row == body) {
            scrollTo(0);
            return;
        }
        // the first row that can take the focus keeps what is above it (titles, headers) on screen
        bool focusableAbove = false;
        for (const auto& child : body->children()) {
            if (child.get() == row) {
                break;
            }
            if (child->visible && child.get() != header_ && child->focusEntry() != nullptr) {
                focusableAbove = true;
                break;
            }
        }
        scrollTo(focusableAbove ? row->frame.y - leadingInset : 0);
        return;
    }
    // view's rectangle in content coordinates
    float top = 0;
    for (const View* v = view; v != nullptr && v != body; v = v->parent()) {
        top += v->frame.y + (v->parent() != nullptr && v->parent() != body ? v->parent()->contentOffset().y : 0);
    }
    const float bottom = top + view->frame.height;
    const float viewport = frame.height - padding.vertical();
    if (top - nearestMargin < offset_) {
        scrollTo(top - nearestMargin);
    } else if (bottom + nearestMargin > offset_ + viewport) {
        scrollTo(bottom + nearestMargin - viewport);
    }
}

}  // namespace playfin::ui
