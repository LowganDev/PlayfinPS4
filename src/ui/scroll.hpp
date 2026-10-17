#pragma once

#include <memory>

#include "ui/view.hpp"

namespace playfin::ui {

// Vertical scrolling container with one content view laid out at its natural height.
// Leading: the row (direct child of the content) holding the focus goes to the top (minus leadingInset).
// Nearest: scroll as little as needed to show the focused view with nearestMargin around it.
class VScroll : public View {
public:
    enum class Policy { Leading, Nearest };

    VScroll();
    Policy policy = Policy::Leading;
    float leadingInset = 0;
    float nearestMargin = 48;
    // > 0 with a Fit height: as tall as its content up to this height
    float maxHeight = 0;

    void setContent(std::unique_ptr<View> content);
    // A focusable row at the top (the navigation bar) that does not count as the first row: the row after it
    // still keeps the content above it on screen.
    void setHeader(const View* header) { header_ = header; }
    View* content() const;
    float scrollOffset() const { return offset_; }

    Size measure(Size available, TextMetrics& metrics) override;
    Point contentOffset() const override { return {0, -offset_}; }
    void onDescendantFocused(View* view) override;

protected:
    void layout(TextMetrics& metrics) override;

private:
    void scrollTo(float target);
    float maxOffset() const;

    const View* header_ = nullptr;
    float offset_ = 0;
    float contentHeight_ = 0;
};

}  // namespace playfin::ui
