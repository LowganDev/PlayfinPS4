#include "app/shell/theme_backdrop.hpp"

#include <nanovg.h>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "ui/application.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

ThemeBackdrop::ThemeBackdrop(AppSession& session) : session_(session) {
    name = "theme backdrop";
    width = ui::SizeSpec::fill();
    height = ui::SizeSpec::fill();
    visible = ui::theme().backdropFollowsFocus;
}

ui::ImageView* ThemeBackdrop::addLayer() {
    auto* layer = add<ui::ImageView>();
    layer->width = ui::SizeSpec::fill();
    layer->height = ui::SizeSpec::fill();
    layer->cornerRadius = 0;
    layer->placeholder = ui::Color{0, 0, 0, 0};
    layer->backdrop = true;
    layer->themedBackdrop = true;
    layer->fadeIn = false;
    return layer;
}

bool ThemeBackdrop::coversScreen() const {
    if (!visible || children().empty()) {
        return false;
    }
    return static_cast<const ui::ImageView*>(children().back().get())->coversFrame();
}

void ThemeBackdrop::draw(NVGcontext* vg) const {
    if (children().size() < 2 || !coversScreen()) {
        ui::Stack::draw(vg);  // the new picture is not there yet: the last one shows under it
        return;
    }
    // the top layer covers the screen: only it is drawn (1.45 million pixels less a frame)
    const ui::View& top = *children().back();
    nvgSave(vg);
    nvgTranslate(vg, top.frame.x, top.frame.y);
    top.draw(vg);
    nvgRestore(vg);
}

std::vector<std::string> ThemeBackdrop::layerUrls() const {
    std::vector<std::string> urls;
    for (const auto& child : children()) {
        urls.push_back(static_cast<const ui::ImageView*>(child.get())->url());
    }
    return urls;
}

ThemeBackdrop::~ThemeBackdrop() {
    if (ui::Application* app = ui::Application::current()) {
        app->cancelTimers(this);
    }
}

void ThemeBackdrop::follow(const jellyfin::Item& item) {
    if (!ui::theme().backdropFollowsFocus) {
        return;
    }
    const auto client = session_.client();
    const std::string url = client ? backdropUrlFor(*client, item, 1920, 1080) : std::string();
    if (url.empty() || url == pending_) {
        return;  // nothing to show: the last picture stays
    }
    pending_ = url;
    ui::Application* app = ui::Application::current();
    if (app == nullptr) {
        return;
    }
    app->cancelTimers(this);
    app->after(kDelayMs, this, [this] {
        if (pending_ == shown_) {
            return;
        }
        shown_ = pending_;
        // the new picture on top, over the last one; the layer under that goes on top for it
        ui::ImageView* top = nullptr;
        if (children().size() < 2) {
            top = addLayer();
        } else {
            addChild(takeChild(children().front().get()));
            top = static_cast<ui::ImageView*>(children().back().get());
        }
        top->setUrl(shown_);
    });
}

void ThemeBackdrop::followFocus(ui::View* focused) {
    for (ui::View* view = focused; view != nullptr; view = view->parent()) {
        if (const auto* card = dynamic_cast<const ItemCard*>(view)) {
            follow(card->item());
            return;
        }
    }
}

void ThemeBackdrop::onThemeChanged() {
    visible = ui::theme().backdropFollowsFocus;
    ui::View::onThemeChanged();
}

}  // namespace playfin::app
