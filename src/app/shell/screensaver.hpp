#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "app/shell/session.hpp"
#include "core/task.hpp"
#include "jellyfin/models.hpp"
#include "ui/box.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

// After a few idle minutes (never over a video): the server's films and series in turn, full screen, with their
// title, synopsis and the time; any button closes it. Shown as a modal, on the dark stage whatever the theme
// (ui::DarkStage first, so its views are built on it).
class Screensaver : private ui::DarkStage, public ui::Stack {
public:
    static constexpr float kIdleMs = 5 * 60 * 1000;

    explicit Screensaver(AppSession& session);
    ~Screensaver() override;
    // from the app's loop: the next picture every 12 s, the clock
    static void tick(float dtMs);
    static bool shown();

private:
    void load();
    void advance(float dtMs);
    void showItem();
    void showClock();

    AppSession& session_;
    ui::ImageView* backdrop_;
    ui::Label* clock_;
    ui::Label* title_;
    ui::Label* overview_;
    std::vector<jellyfin::Item> items_;
    std::size_t index_ = 0;
    float sinceMs_ = 0;
    core::CancelToken token_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

std::unique_ptr<ui::View> makeScreensaver(AppSession& session);

}  // namespace playfin::app
