#include "app/shell/nav_bar.hpp"

#include <nanovg.h>

#include <algorithm>
#include <vector>

#include "app/shell/chrome.hpp"
#include "app/shell/guide.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/screens.hpp"
#include "ui/application.hpp"
#include "ui/draw.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

namespace {

constexpr float kUnderline = 3;

}  // namespace

NavTab::NavTab(std::string text, bool current, std::function<void()> onPress)
    : ui::Box(ui::Axis::Row), current_(current) {
    focusable = true;
    drawsOwnFocus = true;
    padding = {ui::theme().spacingS, ui::theme().spacingL, ui::theme().spacingS, ui::theme().spacingL};
    cornerRadius = ui::theme().radius;
    crossAlign = ui::CrossAlign::Center;
    label_ = add<ui::Label>(std::move(text), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
    bind(platform::Action::Confirm, ui::tr("ui.select"), [onPress = std::move(onPress)] {
        if (onPress) {
            onPress();
        }
        return true;
    });
    refresh();
}

void NavTab::onFocusChanged(bool focused) {
    focused_ = focused;
    refresh();
}

void NavTab::onThemeChanged() {
    refresh();
    ui::Box::onThemeChanged();
}

void NavTab::refresh() {
    const ui::Theme& t = ui::theme();
    const bool pill = t.tabStyle == ui::TabStyle::Pill;
    background = ui::Color{0, 0, 0, 0};
    label_->color = current_ ? t.text : t.textSecondary;
    if (focused_) {
        background = t.controlFocused;
        label_->color = t.onAccent;
    } else if (pill && current_) {
        background = t.tabPillInAccent ? t.accent : t.text;
        label_->color = t.tabPillInAccent ? t.onAccent : t.background;
    } else if (pill) {
        background = t.navBackground.a > 0 ? ui::Color{0, 0, 0, 0} : t.surface;  // inside the bar's pill, or its own
        label_->color = t.text;
    }
    cornerRadius = pill ? frame.height / 2 : t.radius;  // pills: half the height (set again once laid out)
    label_->markDirty();
}

// A pill's radius is exactly half its height: NanoVG clamps a larger radius on each axis apart, which draws an
// ellipse instead of round ends.
void NavTab::layout(ui::TextMetrics& metrics) {
    ui::Box::layout(metrics);
    cornerRadius = ui::theme().tabStyle == ui::TabStyle::Pill ? frame.height / 2 : ui::theme().radius;
}

ui::Color NavTab::labelColor() const { return label_->color; }

bool NavTab::underlineVisible() const {
    return current_ && !focused_ && ui::theme().tabStyle == ui::TabStyle::Underline;
}

void NavTab::draw(NVGcontext* vg) const {
    ui::Box::draw(vg);
    if (badge && badge()) {
        nvgBeginPath(vg);
        nvgCircle(vg, frame.width - 8, 8, 6);
        nvgFillColor(vg, ui::nvgColor(focused_ ? ui::theme().onAccent : ui::theme().accent));
        nvgFill(vg);
    }
    if (underlineVisible()) {
        const float inset = ui::theme().spacingL;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, inset, frame.height - kUnderline, frame.width - 2 * inset, kUnderline, kUnderline / 2);
        nvgFillColor(vg, ui::nvgColor(ui::theme().accent));
        nvgFill(vg);
    }
}

NavBar::NavBar(AppSession& session, Tab current) : ui::Box(ui::Axis::Row) {
    width = ui::SizeSpec::fill();
    crossAlign = ui::CrossAlign::Center;
    gap = ui::theme().spacingS;
    addChild(makeLogo());
    auto* spacer = add<ui::View>();
    spacer->width = ui::SizeSpec::fixed(ui::theme().spacingXl);
    AppSession* shared = &session;
    const std::pair<Tab, const char*> tabs[] = {
        {Tab::Home, "shell.tab.home"},
        {Tab::Search, "shell.tab.search"},
        {Tab::Downloads, "shell.tab.downloads"},
        {Tab::Settings, "shell.tab.settings"}};
    for (const auto& tab : tabs) {
        const Tab which = tab.first;
        std::function<void()> press;
        if (which != current) {  // the current tab is already shown
            press = [shared, which] { openTab(*shared, which); };
        }
        auto* view = add<NavTab>(ui::tr(tab.second), which == current, std::move(press));
        if (which == Tab::Settings) {
            view->badge = [shared] { return shared->update.has_value(); };
        }
        if (which == current) {
            current_ = view;
        }
    }
    add<NowPlayingChip>(session);
    auto* fill = add<ui::View>();
    fill->width = ui::SizeSpec::fill();
    std::string who;
    std::string userName;
    if (const jellyfin::StoredServer* server = session.server()) {
        who = server->name;
        for (const auto& user : server->users) {
            if (user.id == session.data().lastUserId) {
                who += " \xC2\xB7 " + user.name;
                userName = user.name;
            }
        }
    }
    auto* clock = add<ClockLabel>(session);
    clock->margin = {0, ui::theme().spacingL, 0, 0};
    auto* label = add<ui::Label>(who, ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
    label->color = ui::theme().textSecondary;
    label->margin = {0, ui::theme().spacingS, 0, 0};
    addChild(makeAvatar(userName));
}

void NavBar::draw(NVGcontext* vg) const {
    const ui::Color pill = ui::theme().navBackground;
    if (pill.a > 0) {
        float left = 0;
        float right = 0;
        float top = 0;
        float bottom = 0;
        bool any = false;
        for (const auto& child : children()) {
            if (dynamic_cast<const NavTab*>(child.get()) == nullptr) {
                continue;
            }
            const ui::Rect& r = child->frame;
            left = any ? std::min(left, r.x) : r.x;
            right = any ? std::max(right, r.x + r.width) : r.x + r.width;
            top = any ? std::min(top, r.y) : r.y;
            bottom = any ? std::max(bottom, r.y + r.height) : r.y + r.height;
            any = true;
        }
        if (any) {
            const float pad = ui::theme().spacingS;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, left - pad, top - pad, right - left + 2 * pad, bottom - top + 2 * pad,
                           (bottom - top) / 2 + pad);
            nvgFillColor(vg, ui::nvgColor(pill));
            nvgFill(vg);
        }
    }
    ui::Box::draw(vg);
}

namespace {
std::vector<ClockLabel*>& clocks() {
    static std::vector<ClockLabel*> all;
    return all;
}
std::int64_t gMinute = -1;
}  // namespace

ClockLabel::ClockLabel(const AppSession& session)
    : ui::Label("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold}), session_(session) {
    clocks().push_back(this);
    refresh();
}

ClockLabel::~ClockLabel() {
    auto& all = clocks();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

void ClockLabel::refresh() {
    const std::int64_t now = session_.now();
    const std::string text = headerClock(now, session_.utcOffsetMinutes(now));
    if (text != this->text()) {
        setText(text);
    }
}

void ClockLabel::tick(const AppSession& session) {
    const std::int64_t minute = session.now() / 60;
    if (minute == gMinute) {
        return;
    }
    gMinute = minute;
    for (ClockLabel* clock : clocks()) {
        clock->refresh();
    }
}

NowPlayingChip::NowPlayingChip(AppSession& session) : ui::Box(ui::Axis::Row), session_(session) {
    name = "now playing";
    focusable = true;
    drawsOwnFocus = true;
    margin = {0, 0, 0, ui::theme().spacingL};
    padding = {ui::theme().spacingS, ui::theme().spacingM, ui::theme().spacingS, ui::theme().spacingS};
    cornerRadius = ui::theme().radius;
    gap = ui::theme().spacingM;
    crossAlign = ui::CrossAlign::Center;
    cover_ = add<ui::ImageView>();
    cover_->width = ui::SizeSpec::fixed(40);
    cover_->height = ui::SizeSpec::fixed(40);
    cover_->cornerRadius = 6;
    title_ = add<ui::Label>("", ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::SemiBold});
    title_->width = ui::SizeSpec::fixed(260);
    AppSession* shared = &session;
    bind(platform::Action::Confirm, ui::tr("ui.select"), [shared] {
        ui::Application::current()->push(makeNowPlaying(*shared));
        return true;
    });
    session_.music().listen(this, [this] { refresh(); });
    refresh();
}

NowPlayingChip::~NowPlayingChip() { session_.music().unlisten(this); }

void NowPlayingChip::onFocusChanged(bool focused) {
    focused_ = focused;
    refresh();
}

void NowPlayingChip::onThemeChanged() {
    refresh();
    ui::Box::onThemeChanged();
}

void NowPlayingChip::refresh() {
    const MusicPlayer& music = session_.music();
    visible = music.active();
    background = focused_ ? ui::theme().controlFocused : ui::theme().control;
    title_->color = focused_ ? ui::theme().onAccent : ui::theme().text;
    const jellyfin::Item* track = music.status().track;
    if (track != nullptr && track->id != trackId_) {
        trackId_ = track->id;
        title_->setText(track->name);
        if (const auto client = session_.client()) {
            cover_->setUrl(!track->albumId.empty() && !track->images.albumPrimary.empty()
                               ? client->imageUrl(track->albumId, jellyfin::ImageType::Primary,
                                                  track->images.albumPrimary, 80, 80)
                               : std::string());
        }
    }
    if (parent() != nullptr) {
        parent()->markDirty();
    }
    markDirty();
}

void openTab(AppSession& session, Tab tab) {
    ui::Application* app = ui::Application::current();
    if (app == nullptr) {
        return;
    }
    switch (tab) {
    case Tab::Home:
        app->replace(makeHome(session));
        break;
    case Tab::Search:
        app->replace(makeSearch(session));
        break;
    case Tab::Downloads:
        app->replace(makeDownloads(session));
        break;
    case Tab::Settings:
        app->replace(makeSettings(session));
        break;
    }
}

}  // namespace playfin::app
