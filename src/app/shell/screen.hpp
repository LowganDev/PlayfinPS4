#pragma once

#include <functional>
#include <utility>

#include "app/shell/session.hpp"
#include "core/task.hpp"
#include "ui/application.hpp"

namespace playfin::app {

// Shell screens start network jobs with run(): on the worker pool, result on the main thread, dropped when the
// screen is destroyed (cancel token). In test mode jobs run inline. A `done` callback may replace or pop the
// screen: nothing touches the screen after it.
class ShellScreen : public ui::Screen {
public:
    explicit ShellScreen(AppSession& session) : session_(session) {}
    ~ShellScreen() override { token_.cancel(); }

    AppSession& session() { return session_; }
    // A request the user started (connecting, choosing a user) is running: presses meanwhile are ignored.
    bool busy = false;
    // Cancelled when the screen goes away: pass it to network calls so they stop early.
    const core::CancelToken& cancelToken() const { return token_; }

    template <typename T>
    void run(std::function<T()> job, std::function<void(T)> done) {
        ui::Application* app = ui::Application::current();
        if (session_.synchronous || app == nullptr) {
            done(job());
            return;
        }
        core::runAsync<T>(app->pool(), app->mainQueue(), std::move(job), std::move(done), token_);
    }

    // next frame (after the screen is shown and focused)
    void later(std::function<void()> action) {
        ui::Application* app = ui::Application::current();
        if (app == nullptr) {
            return;
        }
        core::CancelToken token = token_;
        app->mainQueue().post([token, action = std::move(action)] {
            if (!token.cancelled()) {
                action();
            }
        });
    }

    // after content arrived: focus it (`target`, else the screen's first focusable view) when nothing of this screen
    // has the focus (and no dialog is open)
    void focusIfEmpty(ui::View* target = nullptr) {
        ui::Application* app = ui::Application::current();
        if (app == nullptr || app->hasModal() || app->top() != this) {
            return;
        }
        ui::View* focused = app->focus().focused();
        if (focused == nullptr || !isAncestorOf(focused) || focused == this) {
            if (target == nullptr || !app->focus().focus(target)) {
                app->focus().focus(this);
            }
        }
    }

    // after the first content arrived: focus `content` when the focus is still where the screen started (`start`,
    // the bar above the content) or outside the screen
    void focusContent(ui::View* content, const ui::View* start) {
        ui::Application* app = ui::Application::current();
        if (app == nullptr || app->hasModal() || app->top() != this) {
            return;
        }
        ui::View* focused = app->focus().focused();
        if (focused == nullptr || focused == start || focused == this || !isAncestorOf(focused)) {
            app->focus().focus(content);
        }
    }

protected:
    AppSession& session_;
    core::CancelToken token_;
};

}  // namespace playfin::app
