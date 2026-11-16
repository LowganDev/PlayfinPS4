#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include "app/shell/session.hpp"
#include "core/task.hpp"
#include "ui/application.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

// Shell screens start network jobs with run(): on the worker pool, result on the main thread, dropped when the
// screen is destroyed (cancel token). In test mode jobs run inline. A `done` callback may replace or pop the
// screen: nothing touches the screen after it.
// While another screen holds the dark stage (the player, the screensaver over this one), the replies and `later`
// actions wait, in order: views built meanwhile would keep the stage's colours (a Label copies the theme's text
// when built). They run once the stage is gone, from the theme change that ends it.
class ShellScreen : public ui::Screen {
public:
    explicit ShellScreen(AppSession& session) : session_(session) {}
    ~ShellScreen() override {
        token_.cancel();
        if (ui::Application* app = ui::Application::current()) {
            app->cancelTimers(this);
        }
    }

    AppSession& session() { return session_; }
    const AppSession& session() const { return session_; }
    // A request the user started (connecting, choosing a user) is running: presses meanwhile are ignored.
    bool busy = false;
    // Cancelled when the screen goes away: pass it to network calls so they stop early.
    const core::CancelToken& cancelToken() const { return token_; }

    template <typename T>
    void run(std::function<T()> job, std::function<void(T)> done) {
        ui::Application* app = ui::Application::current();
        if (session_.synchronous || app == nullptr) {
            deliver<T>(app, done, job());
            return;
        }
        core::runAsync<T>(app->pool(), app->mainQueue(), std::move(job), heldWhileHidden(std::move(done)), token_);
    }
    // the same, on the transfer thread (a long transfer that must not hold the worker pool)
    template <typename T>
    void runTransfer(std::function<T()> job, std::function<void(T)> done) {
        ui::Application* app = ui::Application::current();
        if (session_.synchronous || app == nullptr) {
            deliver<T>(app, done, job());
            return;
        }
        core::runAsync<T>(session_.transfers(), app->mainQueue(), std::move(job), heldWhileHidden(std::move(done)),
                          token_);
    }

    // next frame (after the screen is shown and focused)
    void later(std::function<void()> action) {
        ui::Application* app = ui::Application::current();
        if (app == nullptr) {
            return;
        }
        core::CancelToken token = token_;
        app->mainQueue().post([this, token, action = std::move(action)] {
            if (token.cancelled()) {
                return;
            }
            if (held()) {
                held_.push_back(action);
            } else {
                action();
            }
        });
    }

    // The screen that holds the dark stage itself (the player): its own replies never wait.
    virtual bool onStage() const { return false; }

    // the stage ended (or the theme changed): what waited runs, in order, after this walk over the views
    void onThemeChanged() override {
        ui::Screen::onThemeChanged();
        ui::Application* app = ui::Application::current();
        if (held_.empty() || stageHides() || app == nullptr) {
            return;
        }
        core::CancelToken token = token_;
        app->mainQueue().post([this, token] {
            // each action may pop or replace the screen: once it is gone (cancelled), nothing of it is touched
            while (!token.cancelled() && !held_.empty() && !stageHides()) {
                std::function<void()> action = std::move(held_.front());
                held_.pop_front();
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

private:
    // another screen holds the dark stage
    bool stageHides() const { return ui::darkStage() && !onStage(); }
    // a reply now would wait: behind the stage, or behind earlier replies still waiting
    bool held() const { return stageHides() || !held_.empty(); }

    template <typename T>
    void deliver(ui::Application* app, const std::function<void(T)>& done, T result) {
        if (app != nullptr && held()) {
            auto kept = std::make_shared<T>(std::move(result));
            held_.push_back([done, kept] { done(std::move(*kept)); });
            return;
        }
        done(std::move(result));
    }
    // runAsync calls it on the main thread, only while the screen exists (cancel token)
    template <typename T>
    std::function<void(T)> heldWhileHidden(std::function<void(T)> done) {
        return [this, done = std::move(done)](T result) {
            deliver<T>(ui::Application::current(), done, std::move(result));
        };
    }

    std::deque<std::function<void()>> held_;
};

}  // namespace playfin::app
