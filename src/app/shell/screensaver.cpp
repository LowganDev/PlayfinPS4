#include "app/shell/screensaver.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/guide.hpp"
#include "core/log.hpp"
#include "ui/application.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"

namespace playfin::app {

namespace {

constexpr float kSlideMs = 12000;
constexpr int kPictures = 24;

Screensaver* gShown = nullptr;

}  // namespace

Screensaver::Screensaver(AppSession& session) : session_(session) {
    name = "screensaver";
    background = ui::Color{0, 0, 0, 255};
    focusable = true;
    drawsOwnFocus = true;
    backdrop_ = add<ui::ImageView>();
    backdrop_->width = ui::SizeSpec::fill();
    backdrop_->height = ui::SizeSpec::fill();
    backdrop_->cornerRadius = 0;
    backdrop_->placeholder = ui::Color{0, 0, 0, 255};
    add<BackdropShade>();
    auto* column = add<ui::Box>(ui::Axis::Column);
    column->width = ui::SizeSpec::fill();
    column->height = ui::SizeSpec::fill();
    column->align = ui::Align::End;
    column->padding = {64, 96, 96, 96};
    column->gap = ui::theme().spacingM;
    clock_ = column->add<ui::Label>("", ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
    clock_->color = ui::theme().textSecondary;
    title_ = column->add<ui::Label>("", ui::FontStyle{ui::theme().fontDisplay, ui::FontWeight::Bold});
    title_->width = ui::SizeSpec::fixed(1400);
    title_->maxLines = 2;
    overview_ = column->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
    overview_->width = ui::SizeSpec::fixed(1100);
    overview_->maxLines = 3;
    overview_->color = ui::theme().textSecondary;
    // any button leaves it (and does nothing else)
    for (int i = 0; i < platform::kActionCount; ++i) {
        bind(static_cast<platform::Action>(i), "", [] {
            if (ui::Application* app = ui::Application::current()) {
                app->closeModal();
            }
            return true;
        });
    }
    gShown = this;
    load();
    showClock();
}

Screensaver::~Screensaver() {
    *alive_ = false;
    token_.cancel();
    if (gShown == this) {
        gShown = nullptr;
    }
}

bool Screensaver::shown() { return gShown != nullptr; }

void Screensaver::tick(float dtMs) {
    if (gShown != nullptr) {
        gShown->advance(dtMs);
    }
}

void Screensaver::advance(float dtMs) {
    sinceMs_ += dtMs;
    if (sinceMs_ >= kSlideMs && !items_.empty()) {
        sinceMs_ = 0;
        index_ = (index_ + 1) % items_.size();
        showItem();
    }
    showClock();
}

void Screensaver::showClock() {
    const std::int64_t now = session_.now();
    const std::string text = headerClock(now, session_.utcOffsetMinutes(now));
    if (text != clock_->text()) {
        clock_->setText(text);
    }
}

// films and series with a backdrop, in a random order (the server shuffles)
void Screensaver::load() {
    const auto client = session_.client();
    ui::Application* app = ui::Application::current();
    if (!client || app == nullptr) {
        return;
    }
    jellyfin::ItemQuery query;
    query.includeTypes = {"Movie", "Series"};
    query.sortBy = "Random";
    query.limit = kPictures;
    std::weak_ptr<bool> alive = alive_;
    const core::CancelToken token = token_;
    auto done = [this, alive](core::Result<jellyfin::ItemPage> page) {
        const auto flag = alive.lock();
        if (!flag || !*flag || !page.ok()) {
            return;
        }
        for (const jellyfin::Item& item : page.value().items) {
            if (!item.images.backdrops.empty()) {
                items_.push_back(item);
            }
        }
        index_ = 0;
        showItem();
    };
    if (session_.synchronous) {
        done(client->items(query, token));
        return;
    }
    core::runAsync<core::Result<jellyfin::ItemPage>>(
        app->pool(), app->mainQueue(), [client, query, token] { return client->items(query, token); }, done,
        core::CancelToken{});
}

void Screensaver::showItem() {
    if (items_.empty()) {
        return;
    }
    const jellyfin::Item& item = items_[index_];
    if (const auto client = session_.client()) {
        backdrop_->setUrl(client->imageUrl(item.id, jellyfin::ImageType::Backdrop, item.images.backdrops.front(),
                                           1920, 1080));
    }
    title_->setText(item.name);
    overview_->setText(item.overview);
    overview_->visible = !item.overview.empty();
    markDirty();
}

std::unique_ptr<ui::View> makeScreensaver(AppSession& session) { return std::make_unique<Screensaver>(session); }

}  // namespace playfin::app
