#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "ui/view.hpp"

namespace playfin::ui {

class DataSource {
public:
    virtual ~DataSource() = default;
    virtual int count() const = 0;
    virtual std::unique_ptr<View> create() = 0;
    virtual void bind(View& view, int index) = 0;
};

// Common base of HList and Grid: materializes only the visible items (plus one screen ahead), recycles the rest,
// remembers the focused index, scrolls with an animation.
class VirtualList : public View {
public:
    VirtualList(Size itemSize, float spacing);
    ~VirtualList() override;

    void setSource(std::unique_ptr<DataSource> source);
    DataSource* source() const { return source_.get(); }
    // after the data changed (count or content): rebinds, clamps the focus onto a live item
    void reload();
    void onNearEnd(int threshold, std::function<void()> callback);
    void onFocusIndex(std::function<void(int)> callback);

    int focusedIndex() const { return focused_; }
    View* viewAt(int index) const;
    int materializedCount() const { return static_cast<int>(visible_.size()); }
    float scrollOffset() const { return scroll_; }

    Size itemSize;
    float spacing;

    View* focusEntry() override;
    void onDescendantFocused(View* view) override;
    // the focused item last: it grows over its neighbours
    void draw(NVGcontext* vg) const override;

protected:
    int count() const { return source_ ? source_->count() : 0; }
    int indexOf(const View* view) const;
    View* materialize(int index);
    void layout(TextMetrics& metrics) override;
    void scrollTo(float target);

    virtual Rect itemRect(int index) const = 0;           // in content coordinates
    virtual void visibleRange(int& first, int& last) const = 0;
    virtual float scrollTargetFor(int index) const = 0;
    virtual float maxScroll() const = 0;

    float scroll_ = 0;
    int focused_ = 0;

private:
    void recycle(int index);

    std::unique_ptr<DataSource> source_;
    std::map<int, View*> visible_;
    std::vector<std::unique_ptr<View>> pool_;
    int nearEndThreshold_ = 0;
    int nearEndFiredAt_ = -1;
    std::function<void()> nearEnd_;
    std::function<void(int)> focusIndex_;
};

class HList : public VirtualList {
public:
    HList(Size itemSize, float spacing);
    Size measure(Size available, TextMetrics& metrics) override;
    Point contentOffset() const override { return {-scroll_, 0}; }
    View* navigate(View* from, Direction direction) override;

protected:
    Rect itemRect(int index) const override;
    void visibleRange(int& first, int& last) const override;
    float scrollTargetFor(int index) const override;
    float maxScroll() const override;
};

class Grid : public VirtualList {
public:
    Grid(Size itemSize, float spacing, int columns);
    int columns;
    Size measure(Size available, TextMetrics& metrics) override;
    Point contentOffset() const override { return {0, -scroll_}; }
    View* navigate(View* from, Direction direction) override;

protected:
    Rect itemRect(int index) const override;
    void visibleRange(int& first, int& last) const override;
    float scrollTargetFor(int index) const override;
    float maxScroll() const override;
};

}  // namespace playfin::ui
