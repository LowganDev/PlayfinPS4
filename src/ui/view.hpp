#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/input.hpp"
#include "ui/geometry.hpp"
#include "ui/text.hpp"
#include "ui/theme.hpp"

struct NVGcontext;

namespace playfin::ui {

enum class Direction { Up, Down, Left, Right };
enum class SizeMode { Fixed, Fill, Fit };

struct SizeSpec {
    SizeMode mode = SizeMode::Fit;
    float value = 0;  // pixels (Fixed) or weight (Fill)
    static SizeSpec fixed(float pixels) { return {SizeMode::Fixed, pixels}; }
    static SizeSpec fill(float weight = 1) { return {SizeMode::Fill, weight}; }
    static SizeSpec fit() { return {SizeMode::Fit, 0}; }
};

struct ActionBinding {
    platform::Action action;
    std::string label;  // shown in the hints bar when not empty
    std::function<bool()> handler;
};

// Base of every widget: owns its children, lays them out, draws in local coordinates (0,0 = own top-left),
// takes part in controller focus. Main thread only.
class View {
public:
    View() = default;
    virtual ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // tree
    View* parent() const { return parent_; }
    const std::vector<std::unique_ptr<View>>& children() const { return children_; }
    template <typename T, typename... Args>
    T* add(Args&&... args) {
        auto view = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = view.get();
        addChild(std::move(view));
        return raw;
    }
    View* addChild(std::unique_ptr<View> child);
    std::unique_ptr<View> takeChild(View* child);
    void clearChildren();
    bool isAncestorOf(const View* view) const;     // inclusive; `view` must be alive
    bool containsPointer(const View* view) const;  // inclusive; never dereferences `view`

    // layout parameters
    SizeSpec width;
    SizeSpec height;
    Insets margin;
    Insets padding;
    bool visible = true;
    float opacity = 1;  // multiplies the parent's (transitions)
    float shiftY = 0;   // drawn this much lower than its frame (transitions; focus and hit tests ignore it)

    // appearance
    Color background{0, 0, 0, 0};
    float cornerRadius = 0;
    bool clip = false;

    // geometry: frame relative to the parent's top-left (before the parent's content offset)
    Rect frame;
    Rect absoluteFrame() const;
    virtual Point contentOffset() const { return {0, 0}; }

    virtual Size measure(Size available, TextMetrics& metrics);
    void place(const Rect& rect, TextMetrics& metrics);
    virtual void draw(NVGcontext* vg) const;
    void drawBackground(NVGcontext* vg) const;
    // `last`: a child drawn after the others (on top), or nullptr
    void drawChildren(NVGcontext* vg, const View* last = nullptr) const;

    // focus
    bool focusable = false;
    bool drawsOwnFocus = false;  // shows its focus itself: no focus ring around it
    bool rememberFocus = false;
    View* remembered = nullptr;  // last focused descendant (compared by address only)
    virtual bool canFocus() const { return focusable && visible; }
    virtual void onFocusChanged(bool focused) { (void)focused; }
    // The theme changed (accent setting): views that copied theme colours take them again. Recursive.
    virtual void onThemeChanged();
    virtual View* focusEntry();
    virtual View* navigate(View* from, Direction direction) {
        (void)from;
        (void)direction;
        return nullptr;
    }
    virtual void onDescendantFocused(View* view) { (void)view; }
    void setNextFocus(Direction direction, View* target) { overrides_[static_cast<int>(direction)] = target; }
    View* nextFocus(Direction direction) const { return overrides_[static_cast<int>(direction)]; }

    // actions
    void bind(platform::Action action, std::string label, std::function<bool()> handler);
    const std::vector<ActionBinding>& bindings() const { return bindings_; }

    void markDirty();
    bool needsLayout() const { return dirty_; }

    std::string name;  // for logs and debugging

protected:
    virtual void layout(TextMetrics& metrics);
    // for virtualized lists: attach / detach without marking the tree dirty
    View* adopt(std::unique_ptr<View> child);
    std::unique_ptr<View> release(View* child);

private:
    View* parent_ = nullptr;
    std::vector<std::unique_ptr<View>> children_;
    std::vector<ActionBinding> bindings_;
    View* overrides_[4] = {nullptr, nullptr, nullptr, nullptr};
    bool dirty_ = true;
};

// Size of `view` for the given available space (margins excluded): Fixed, Fill (all of it) or Fit (measured).
Size resolveSize(View& view, Size available, TextMetrics& metrics);

}  // namespace playfin::ui
