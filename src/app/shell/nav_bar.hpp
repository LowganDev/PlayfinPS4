#pragma once

#include <functional>
#include <string>

#include "app/shell/session.hpp"
#include "ui/box.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"

namespace playfin::app {

enum class Tab { Home, Search, Settings };

// A tab of the navigation bar: focused, it fills with the accent; the current tab is underlined with it.
class NavTab : public ui::Box {
public:
    NavTab(std::string text, bool current, std::function<void()> onPress);
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;
    void draw(NVGcontext* vg) const override;

private:
    void refresh();
    bool current_;
    bool focused_ = false;
    ui::Label* label_;
};

// The top line of the tab screens (home, search, settings): the logo, the three tabs, then "server · user" and
// the user's avatar. ✕ on a tab replaces the screen stack with that tab's screen.
class NavBar : public ui::Box {
public:
    NavBar(AppSession& session, Tab current);
    NavTab* currentTab() const { return current_; }

private:
    NavTab* current_ = nullptr;
};

// After the tabs while music plays: the album cover and the track; ✕ opens Now playing. Named "now playing".
class NowPlayingChip : public ui::Box {
public:
    explicit NowPlayingChip(AppSession& session);
    ~NowPlayingChip() override;
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;

private:
    void refresh();
    AppSession& session_;
    ui::ImageView* cover_;
    ui::Label* title_;
    std::string trackId_;
    bool focused_ = false;
};

// The header's date and time ("Vendredi 16 · 21:45"); every one shown is set again when the minute changes.
class ClockLabel : public ui::Label {
public:
    explicit ClockLabel(const AppSession& session);
    ~ClockLabel() override;
    void refresh();
    // once per frame from the app's loop: when the minute changed, every clock shown takes the new time
    static void tick(const AppSession& session);

private:
    const AppSession& session_;
};

// The tab's screen replaces the whole stack.
void openTab(AppSession& session, Tab tab);

}  // namespace playfin::app
