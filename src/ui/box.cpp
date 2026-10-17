#include "ui/box.hpp"

#include <algorithm>
#include <vector>

namespace playfin::ui {

namespace {

float mainOf(Axis axis, Size size) { return axis == Axis::Row ? size.width : size.height; }
float crossOf(Axis axis, Size size) { return axis == Axis::Row ? size.height : size.width; }
float mainMargins(Axis axis, const Insets& m) { return axis == Axis::Row ? m.horizontal() : m.vertical(); }
float crossMargins(Axis axis, const Insets& m) { return axis == Axis::Row ? m.vertical() : m.horizontal(); }
float mainStart(Axis axis, const Insets& m) { return axis == Axis::Row ? m.left : m.top; }
float crossStart(Axis axis, const Insets& m) { return axis == Axis::Row ? m.top : m.left; }
const SizeSpec& mainSpec(Axis axis, const View& view) { return axis == Axis::Row ? view.width : view.height; }
const SizeSpec& crossSpec(Axis axis, const View& view) { return axis == Axis::Row ? view.height : view.width; }

float aligned(CrossAlign align, float space, float size) {
    switch (align) {
    case CrossAlign::Center:
        return (space - size) / 2;
    case CrossAlign::End:
        return space - size;
    default:
        return 0;
    }
}

}  // namespace

Size Box::measure(Size available, TextMetrics& metrics) {
    const Size inner{std::max(0.0f, available.width - padding.horizontal()),
                     std::max(0.0f, available.height - padding.vertical())};
    float mainSum = 0;
    float crossMax = 0;
    int count = 0;
    for (const auto& child : children()) {
        if (!child->visible) {
            continue;
        }
        const Size size = resolveSize(*child, inner, metrics);
        const float main = mainSpec(axis, *child).mode == SizeMode::Fill ? 0 : mainOf(axis, size);
        mainSum += main + mainMargins(axis, child->margin);
        crossMax = std::max(crossMax, crossOf(axis, size) + crossMargins(axis, child->margin));
        ++count;
    }
    if (count > 1) {
        mainSum += gap * static_cast<float>(count - 1);
    }
    return axis == Axis::Row ? Size{mainSum + padding.horizontal(), crossMax + padding.vertical()}
                             : Size{crossMax + padding.horizontal(), mainSum + padding.vertical()};
}

void Box::layout(TextMetrics& metrics) {
    const Size inner{std::max(0.0f, frame.width - padding.horizontal()),
                     std::max(0.0f, frame.height - padding.vertical())};
    const float innerMain = mainOf(axis, inner);
    const float innerCross = crossOf(axis, inner);

    std::vector<View*> items;
    std::vector<Size> sizes;
    float used = 0;
    float weights = 0;
    for (const auto& child : children()) {
        if (!child->visible) {
            continue;
        }
        items.push_back(child.get());
        const Size size = resolveSize(*child, inner, metrics);
        sizes.push_back(size);
        if (mainSpec(axis, *child).mode == SizeMode::Fill) {
            weights += mainSpec(axis, *child).value;
            used += mainMargins(axis, child->margin);
        } else {
            used += mainOf(axis, size) + mainMargins(axis, child->margin);
        }
    }
    if (items.empty()) {
        return;
    }
    used += gap * static_cast<float>(items.size() - 1);
    const float remaining = std::max(0.0f, innerMain - used);

    float cursor = 0;
    float extraGap = 0;
    if (weights <= 0) {
        switch (align) {
        case Align::Center:
            cursor = remaining / 2;
            break;
        case Align::End:
            cursor = remaining;
            break;
        case Align::SpaceBetween:
            extraGap = items.size() > 1 ? remaining / static_cast<float>(items.size() - 1) : 0;
            break;
        case Align::Start:
            break;
        }
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        View* child = items[i];
        const SizeSpec& spec = mainSpec(axis, *child);
        float main = spec.mode == SizeMode::Fill ? (weights > 0 ? remaining * spec.value / weights : 0)
                                                 : mainOf(axis, sizes[i]);
        float cross = crossOf(axis, sizes[i]);
        if (crossAlign == CrossAlign::Stretch && crossSpec(axis, *child).mode != SizeMode::Fixed) {
            cross = std::max(0.0f, innerCross - crossMargins(axis, child->margin));
        }
        const float crossSpace = innerCross - crossMargins(axis, child->margin);
        const float crossPos = crossStart(axis, child->margin) + aligned(crossAlign, crossSpace, cross);
        const float mainPos = cursor + mainStart(axis, child->margin);
        Rect rect = axis == Axis::Row ? Rect{padding.left + mainPos, padding.top + crossPos, main, cross}
                                      : Rect{padding.left + crossPos, padding.top + mainPos, cross, main};
        child->place(rect, metrics);
        cursor += main + mainMargins(axis, child->margin) + gap + extraGap;
    }
}

void Stack::layout(TextMetrics& metrics) {
    const Size inner{std::max(0.0f, frame.width - padding.horizontal()),
                     std::max(0.0f, frame.height - padding.vertical())};
    for (const auto& child : children()) {
        if (!child->visible) {
            continue;
        }
        const Size size = resolveSize(*child, inner, metrics);
        const float x = child->margin.left + aligned(horizontal, inner.width - child->margin.horizontal(), size.width);
        const float y = child->margin.top + aligned(vertical, inner.height - child->margin.vertical(), size.height);
        child->place({padding.left + x, padding.top + y, size.width, size.height}, metrics);
    }
}

}  // namespace playfin::ui
