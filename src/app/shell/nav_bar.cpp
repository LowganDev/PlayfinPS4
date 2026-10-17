#include "app/shell/nav_bar.hpp"

#include <nanovg.h>

#include "app/shell/chrome.hpp"
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
    background = focused_ ? ui::theme().controlFocused : ui::Color{0, 0, 0, 0};
    label_->color = focused_ ? ui::theme().onAccent : current_ ? ui::theme().text : ui::theme().textSecondary;
    label_->markDirty();
}

void NavTab::draw(NVGcontext* vg) const {
    ui::Box::draw(vg);
    if (current_ && !focused_) {
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
        {Tab::Home, "shell.tab.home"}, {Tab::Search, "shell.tab.search"}, {Tab::Settings, "shell.tab.settings"}};
    for (const auto& tab : tabs) {
        const Tab which = tab.first;
        std::function<void()> press;
        if (which != current) {  // the current tab is already shown
            press = [shared, which] { openTab(*shared, which); };
        }
        auto* view = add<NavTab>(ui::tr(tab.second), which == current, std::move(press));
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
    auto* label = add<ui::Label>(who, ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
    label->color = ui::theme().textSecondary;
    label->margin = {0, ui::theme().spacingS, 0, 0};
    addChild(makeAvatar(userName));
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
    case Tab::Settings:
        app->replace(makeSettings(session));
        break;
    }
}

}  // namespace playfin::app
