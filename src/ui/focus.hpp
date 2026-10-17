#pragma once

#include <vector>

#include "ui/view.hpp"

namespace playfin::ui {

class FocusManager {
public:
    View* focused() const { return focused_; }
    View* scope() const { return scope_; }
    void setScope(View* scope) { scope_ = scope; }
    bool focus(View* view);
    bool move(Direction direction);
    void forget(View* view);
    // Bindings of the focused view and its ancestors up to (and including) the scope, innermost first
    std::vector<const ActionBinding*> actionChain() const;
    // Runs the first binding for `action`; directions without a binding move the focus.
    bool dispatch(platform::Action action);

private:
    View* focused_ = nullptr;
    View* scope_ = nullptr;
};

}  // namespace playfin::ui
