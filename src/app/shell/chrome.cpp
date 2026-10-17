#include "app/shell/chrome.hpp"

#include <nanovg.h>

#include "ui/box.hpp"
#include "ui/draw.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

namespace {

// a label in the accent colour, following the accent setting
class AccentLabel : public ui::Label {
public:
    AccentLabel(std::string text, ui::FontStyle style) : ui::Label(std::move(text), style) {
        color = ui::theme().accent;
    }
    void onThemeChanged() override {
        color = ui::theme().accent;
        ui::Label::onThemeChanged();
    }
};

class Avatar : public ui::Stack {
public:
    explicit Avatar(const std::string& initial) {
        width = ui::SizeSpec::fixed(48);
        height = ui::SizeSpec::fixed(48);
        cornerRadius = 24;
        horizontal = ui::CrossAlign::Center;
        vertical = ui::CrossAlign::Center;
        label_ = add<ui::Label>(initial, ui::FontStyle{22, ui::FontWeight::Bold});
        onThemeChanged();
    }
    void onThemeChanged() override {
        background = ui::theme().accent;
        label_->color = ui::theme().onAccent;
        ui::Stack::onThemeChanged();
    }

private:
    ui::Label* label_;
};


}  // namespace

ui::FontStyle bodyText() { return {ui::theme().fontBody, ui::FontWeight::Regular}; }
ui::FontStyle bodyStrong() { return {ui::theme().fontBody, ui::FontWeight::SemiBold}; }
ui::FontStyle headingText() { return {ui::theme().fontHeading, ui::FontWeight::SemiBold}; }
ui::FontStyle titleText(ui::FontWeight weight) { return {ui::theme().fontTitle, weight}; }

ui::Insets rowPadding() {
    return {ui::theme().spacingM, ui::theme().spacingL, ui::theme().spacingM, ui::theme().spacingL};
}

ui::Insets pagePadding() { return {ui::theme().spacingXxl, 96, 120, 96}; }

std::string initialOf(const std::string& name) {
    if (name.empty()) {
        return "?";
    }
    std::size_t length = 1;
    const auto lead = static_cast<unsigned char>(name[0]);
    if (lead >= 0xF0) {
        length = 4;
    } else if (lead >= 0xE0) {
        length = 3;
    } else if (lead >= 0xC0) {
        length = 2;
    }
    std::string initial = name.substr(0, length);
    if (initial.size() == 1 && initial[0] >= 'a' && initial[0] <= 'z') {
        initial[0] = static_cast<char>(initial[0] - 'a' + 'A');
    }
    return initial;
}

std::unique_ptr<ui::View> makeLogo() {
    auto logo = std::make_unique<ui::Box>(ui::Axis::Row);
    const ui::FontStyle style{ui::theme().fontLogo, ui::FontWeight::Bold};
    logo->add<ui::Label>("Playfin", style);
    logo->add<AccentLabel>(".", style);
    return logo;
}

std::unique_ptr<ui::View> makeAvatar(const std::string& userName) { return std::make_unique<Avatar>(initialOf(userName)); }

BackdropShade::BackdropShade() {
    width = ui::SizeSpec::fill();
    height = ui::SizeSpec::fill();
}

void BackdropShade::draw(NVGcontext* vg) const {
    const ui::Color bg = ui::theme().background;
    const float split = frame.height * 0.7f;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, frame.width, split);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, split, nvgRGBA(bg.r, bg.g, bg.b, 143), nvgRGBA(bg.r, bg.g, bg.b, 255)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, split, frame.width, frame.height - split);
    nvgFillColor(vg, nvgRGBA(bg.r, bg.g, bg.b, 255));
    nvgFill(vg);
}

std::string backdropUrlFor(const jellyfin::Client& client, const jellyfin::Item& item, int width, int height) {
    const jellyfin::ImageTags& images = item.images;
    if (!images.backdrops.empty()) {
        return client.imageUrl(item.id, jellyfin::ImageType::Backdrop, images.backdrops.front(), width, height);
    }
    if (!images.parentBackdropItemId.empty() && !images.parentBackdrops.empty()) {
        return client.imageUrl(images.parentBackdropItemId, jellyfin::ImageType::Backdrop,
                               images.parentBackdrops.front(), width, height);
    }
    return {};
}

}  // namespace playfin::app
