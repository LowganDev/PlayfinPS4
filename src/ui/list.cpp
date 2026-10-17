#include "ui/list.hpp"

#include <algorithm>
#include <cmath>

#include "ui/animation.hpp"
#include "ui/context.hpp"
#include "ui/focus.hpp"

namespace playfin::ui {

VirtualList::~VirtualList() = default;

VirtualList::VirtualList(Size itemSizeValue, float spacingValue) : itemSize(itemSizeValue), spacing(spacingValue) {
    clip = true;
    rememberFocus = true;
}

void VirtualList::setSource(std::unique_ptr<DataSource> source) {
    FocusManager* focus = context().focus;
    View* focused = focus != nullptr ? focus->focused() : nullptr;
    const bool hadFocus = focused != nullptr && containsPointer(focused);
    if (hadFocus) {
        focus->forget(focused);
    }
    // the old views were made by the old source: they are not reused for the new one
    for (const auto& entry : visible_) {
        release(entry.second).reset();
    }
    visible_.clear();
    pool_.clear();
    source_ = std::move(source);
    focused_ = 0;
    scroll_ = 0;
    nearEndFiredAt_ = -1;
    markDirty();
    if (hadFocus && focus != nullptr && count() > 0) {
        focus->focus(materialize(0));
    }
}

void VirtualList::reload() {
    const int total = count();
    View* focused = context().focus != nullptr ? context().focus->focused() : nullptr;
    const bool hadFocus = focused != nullptr && containsPointer(focused);
    focused_ = total == 0 ? 0 : std::clamp(focused_, 0, total - 1);
    std::vector<int> indices;
    for (const auto& entry : visible_) {
        indices.push_back(entry.first);
    }
    for (int index : indices) {
        if (index >= total) {
            recycle(index);
        } else {
            source_->bind(*visible_[index], index);
        }
    }
    if (nearEndFiredAt_ >= 0 && total != nearEndFiredAt_) {
        nearEndFiredAt_ = -1;
    }
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll());
    markDirty();
    if (hadFocus && context().focus != nullptr && total > 0) {
        context().focus->focus(materialize(focused_));
    }
}

void VirtualList::onNearEnd(int threshold, std::function<void()> callback) {
    nearEndThreshold_ = threshold;
    nearEnd_ = std::move(callback);
}

void VirtualList::onFocusIndex(std::function<void(int)> callback) { focusIndex_ = std::move(callback); }

void VirtualList::draw(NVGcontext* vg) const {
    drawBackground(vg);
    drawChildren(vg, viewAt(focused_));
}

View* VirtualList::viewAt(int index) const {
    auto it = visible_.find(index);
    return it == visible_.end() ? nullptr : it->second;
}

int VirtualList::indexOf(const View* view) const {
    for (const auto& entry : visible_) {
        if (entry.second->containsPointer(view)) {
            return entry.first;
        }
    }
    return -1;
}

View* VirtualList::materialize(int index) {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    if (View* existing = viewAt(index)) {
        return existing;
    }
    std::unique_ptr<View> view;
    if (!pool_.empty()) {
        view = std::move(pool_.back());
        pool_.pop_back();
    } else {
        view = source_->create();
    }
    source_->bind(*view, index);
    View* raw = adopt(std::move(view));
    visible_[index] = raw;
    if (context().metrics != nullptr) {
        const Rect rect = itemRect(index);
        raw->place(rect, *context().metrics);
    } else {
        raw->frame = itemRect(index);
    }
    return raw;
}

void VirtualList::recycle(int index) {
    auto it = visible_.find(index);
    if (it == visible_.end()) {
        return;
    }
    if (context().focus != nullptr && it->second->containsPointer(context().focus->focused())) {
        context().focus->forget(context().focus->focused());
    }
    pool_.push_back(release(it->second));
    visible_.erase(it);
}

void VirtualList::layout(TextMetrics& metrics) {
    const int total = count();
    int first = 0;
    int last = -1;
    if (total > 0) {
        visibleRange(first, last);
        first = std::max(0, first);
        last = std::min(total - 1, last);
    }
    std::vector<int> stale;
    for (const auto& entry : visible_) {
        const bool focusedItem = entry.first == focused_;
        if ((entry.first < first || entry.first > last) && !focusedItem) {
            stale.push_back(entry.first);
        }
    }
    for (int index : stale) {
        recycle(index);
    }
    for (int index = first; index <= last; ++index) {
        materialize(index);
    }
    for (const auto& entry : visible_) {
        entry.second->place(itemRect(entry.first), metrics);
    }
}

void VirtualList::scrollTo(float target) {
    target = std::clamp(target, 0.0f, maxScroll());
    if (Animator* animator = context().animator) {
        animator->animate(&scroll_, target, theme().scrollMs, Easing::EaseOutCubic, this);
    } else {
        scroll_ = target;
    }
    markDirty();
}

View* VirtualList::focusEntry() {
    if (!visible || count() == 0) {
        return nullptr;
    }
    focused_ = std::clamp(focused_, 0, count() - 1);
    View* item = materialize(focused_);
    return item != nullptr ? item->focusEntry() : nullptr;
}

void VirtualList::onDescendantFocused(View* view) {
    const int index = indexOf(view);
    if (index < 0) {
        return;
    }
    focused_ = index;
    scrollTo(scrollTargetFor(index));
    if (focusIndex_) {
        focusIndex_(index);
    }
    const int total = count();
    if (nearEnd_ && index >= total - nearEndThreshold_ && nearEndFiredAt_ != total) {
        nearEndFiredAt_ = total;
        nearEnd_();
    }
}

// ---- HList

HList::HList(Size itemSizeValue, float spacingValue) : VirtualList(itemSizeValue, spacingValue) {
    width = SizeSpec::fill();
}

Size HList::measure(Size available, TextMetrics& metrics) {
    (void)metrics;
    return {available.width, itemSize.height + padding.vertical()};
}

Rect HList::itemRect(int index) const {
    return {padding.left + static_cast<float>(index) * (itemSize.width + spacing), padding.top, itemSize.width,
            itemSize.height};
}

void HList::visibleRange(int& first, int& last) const {
    const float stride = itemSize.width + spacing;
    const float viewport = frame.width;
    const int perScreen = static_cast<int>(std::ceil(static_cast<double>(viewport / stride))) + 1;
    first = static_cast<int>(std::floor(static_cast<double>(scroll_ / stride))) - 1;
    last = first + 1 + 2 * perScreen;
}

float HList::scrollTargetFor(int index) const { return static_cast<float>(index) * (itemSize.width + spacing); }

float HList::maxScroll() const {
    const int total = count();
    const float content = padding.horizontal() + static_cast<float>(total) * (itemSize.width + spacing) - spacing;
    return std::max(0.0f, content - frame.width);
}

View* HList::navigate(View* from, Direction direction) {
    if (direction != Direction::Left && direction != Direction::Right) {
        return nullptr;
    }
    const int index = indexOf(from);
    if (index < 0) {
        return nullptr;
    }
    return materialize(direction == Direction::Right ? index + 1 : index - 1);
}

// ---- Grid

Grid::Grid(Size itemSizeValue, float spacingValue, int columnsValue)
    : VirtualList(itemSizeValue, spacingValue), columns(std::max(1, columnsValue)) {
    width = SizeSpec::fill();
    height = SizeSpec::fill();
}

Size Grid::measure(Size available, TextMetrics& metrics) {
    (void)metrics;
    return available;
}

Rect Grid::itemRect(int index) const {
    const int row = index / columns;
    const int column = index % columns;
    return {padding.left + static_cast<float>(column) * (itemSize.width + spacing),
            padding.top + static_cast<float>(row) * (itemSize.height + spacing), itemSize.width, itemSize.height};
}

void Grid::visibleRange(int& first, int& last) const {
    const float stride = itemSize.height + spacing;
    const int rowsPerScreen = static_cast<int>(std::ceil(static_cast<double>(frame.height / stride))) + 1;
    const int firstRow = static_cast<int>(std::floor(static_cast<double>(scroll_ / stride))) - 1;
    first = firstRow * columns;
    last = (firstRow + 1 + 2 * rowsPerScreen) * columns - 1;
}

float Grid::scrollTargetFor(int index) const {
    const float stride = itemSize.height + spacing;
    const float top = static_cast<float>(index / columns) * stride;
    const float bottom = top + itemSize.height + padding.vertical();
    if (top < scroll_) {
        return top;
    }
    if (bottom > scroll_ + frame.height) {
        return bottom - frame.height;
    }
    return scroll_;
}

float Grid::maxScroll() const {
    const int rows = (count() + columns - 1) / columns;
    const float content = padding.vertical() + static_cast<float>(rows) * (itemSize.height + spacing) - spacing;
    return std::max(0.0f, content - frame.height);
}

View* Grid::navigate(View* from, Direction direction) {
    const int index = indexOf(from);
    if (index < 0) {
        return nullptr;
    }
    const int total = count();
    switch (direction) {
    case Direction::Left:
        return index % columns == 0 ? nullptr : materialize(index - 1);
    case Direction::Right:
        return (index % columns == columns - 1 || index + 1 >= total) ? nullptr : materialize(index + 1);
    case Direction::Up:
        return index - columns < 0 ? nullptr : materialize(index - columns);
    case Direction::Down: {
        if (index + columns < total) {
            return materialize(index + columns);
        }
        const int lastRowStart = ((total - 1) / columns) * columns;
        return index < lastRowStart ? materialize(total - 1) : nullptr;
    }
    }
    return nullptr;
}

}  // namespace playfin::ui
