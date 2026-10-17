#include "ui/widgets.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>

#include "ui/application.hpp"
#include "ui/context.hpp"
#include "ui/draw.hpp"
#include "ui/i18n.hpp"
#include "ui/scroll.hpp"

namespace playfin::ui {

Button::Button(std::string text, std::function<void()> onPress) : Box(Axis::Row) {
    focusable = true;
    drawsOwnFocus = true;
    padding = {theme().spacingM, theme().spacingL, theme().spacingM, theme().spacingL};
    cornerRadius = theme().radius;
    background = theme().control;
    crossAlign = CrossAlign::Center;
    label_ = add<Label>(std::move(text), FontStyle{theme().fontBody, FontWeight::Regular});
    bind(platform::Action::Confirm, tr("ui.select"), [onPress = std::move(onPress)] {
        if (onPress) {
            onPress();
        }
        return true;
    });
}

void Button::setText(std::string text) { label_->setText(std::move(text)); }

void Button::setPrimary(bool primary) {
    primary_ = primary;
    refresh();
}

void Button::onFocusChanged(bool focused) {
    focused_ = focused;
    refresh();
}

void Button::onThemeChanged() {
    refresh();
    Box::onThemeChanged();
}

void Button::refresh() {
    const bool lit = focused_ || primary_;
    background = lit ? theme().controlFocused : theme().control;
    label_->color = lit ? theme().onAccent : theme().text;
    label_->style.weight = lit ? FontWeight::SemiBold : FontWeight::Regular;
    label_->markDirty();
}

ProgressBar::ProgressBar() {
    height = SizeSpec::fixed(6);
    width = SizeSpec::fill();
}

void ProgressBar::draw(NVGcontext* vg) const {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, frame.width, frame.height, frame.height / 2);
    nvgFillColor(vg, nvgColor(withAlpha(theme().text, 0x40)));
    nvgFill(vg);
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    if (clamped > 0) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, frame.width * clamped, frame.height, frame.height / 2);
        nvgFillColor(vg, nvgColor(theme().accent));
        nvgFill(vg);
    }
}

Spinner::Spinner() {
    width = SizeSpec::fixed(48);
    height = SizeSpec::fixed(48);
}

void Spinner::draw(NVGcontext* vg) const {
    context().redrawNextFrame = true;  // the arc turns with the clock
    const float radius = std::min(frame.width, frame.height) / 2 - 4;
    constexpr float kTurn = 6.2831853f;  // M_PI is not defined in strict C++17 mode
    const float start = context().timeMs / 1000.0f * kTurn;
    nvgBeginPath(vg);
    nvgArc(vg, frame.width / 2, frame.height / 2, radius, start, start + 4.2f, NVG_CW);
    nvgStrokeColor(vg, nvgColor(theme().accent));
    nvgStrokeWidth(vg, 5);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
}

TextField::TextField(std::string placeholder, platform::TextInputRequest request,
                     std::function<void(const std::string&)> onChange)
    : Box(Axis::Row), placeholder_(std::move(placeholder)), request_(std::move(request)),
      onChange_(std::move(onChange)) {
    focusable = true;
    drawsOwnFocus = true;
    width = SizeSpec::fixed(520);
    padding = {theme().spacingM, theme().spacingL, theme().spacingM, theme().spacingL};
    cornerRadius = theme().radius;
    background = theme().control;
    label_ = add<Label>(placeholder_, FontStyle{theme().fontBody, FontWeight::Regular});
    label_->width = SizeSpec::fill();
    refresh();
    bind(platform::Action::Confirm, tr("ui.edit"), [this] {
        Application* app = Application::current();
        if (app == nullptr) {
            return false;
        }
        platform::TextInputRequest request = request_;
        request.initial = value_;
        app->requestText(request, [this, alive = std::weak_ptr<bool>(alive_)](std::optional<std::string> text) {
            if (alive.expired()) {
                return;
            }
            if (text) {
                setValue(*text);
                if (onChange_) {
                    onChange_(value_);
                }
            }
        });
        return true;
    });
}

void TextField::setValue(std::string value) {
    value_ = std::move(value);
    refresh();
}

void TextField::refresh() {
    background = focused_ ? theme().controlFocused : theme().control;
    const Color ink = focused_ ? theme().onAccent : theme().text;
    if (value_.empty()) {
        label_->setText(placeholder_);
        label_->color = focused_ ? withAlpha(ink, 0xB0) : theme().textSecondary;
    } else if (request_.kind == platform::TextInputRequest::Kind::Password) {
        std::string dots;
        for (std::size_t i = 0; i < std::min<std::size_t>(value_.size(), 32); ++i) {
            dots += "\xE2\x80\xA2";
        }
        label_->setText(dots);
        label_->color = ink;
    } else {
        label_->setText(value_);
        label_->color = ink;
    }
    label_->markDirty();
}

void TextField::onFocusChanged(bool focused) {
    focused_ = focused;
    refresh();
}

void TextField::onThemeChanged() {
    refresh();
    Box::onThemeChanged();
}

ValueRow::ValueRow(std::string label, std::string value, std::function<void()> onPress) : Box(Axis::Row) {
    focusable = true;
    drawsOwnFocus = true;
    width = SizeSpec::fixed(1100);
    padding = {theme().spacingM + 2, theme().spacingL, theme().spacingM + 2, theme().spacingL};
    cornerRadius = theme().radius;
    align = Align::SpaceBetween;
    crossAlign = CrossAlign::Center;
    gap = theme().spacingL;
    label_ = add<Label>(std::move(label), FontStyle{theme().fontBody, FontWeight::Regular});
    value_ = add<Label>(std::move(value), FontStyle{theme().fontBody, FontWeight::Regular});
    refresh();
    if (onPress) {
        bind(platform::Action::Confirm, tr("ui.select"), [onPress = std::move(onPress)] {
            onPress();
            return true;
        });
    }
}

void ValueRow::setValue(std::string value) { value_->setText(std::move(value)); }

const std::string& ValueRow::value() const { return value_->text(); }

void ValueRow::onFocusChanged(bool focused) {
    focused_ = focused;
    refresh();
}

void ValueRow::onThemeChanged() {
    refresh();
    Box::onThemeChanged();
}

void ValueRow::refresh() {
    background = focused_ ? theme().controlFocused : theme().control;
    label_->color = focused_ ? theme().onAccent : theme().text;
    label_->style.weight = focused_ ? FontWeight::SemiBold : FontWeight::Regular;
    value_->color = focused_ ? theme().onAccent : theme().textSecondary;
    label_->markDirty();
    value_->markDirty();
}

std::unique_ptr<View> makeChoiceDialog(std::string title, std::vector<std::string> choices, int selected,
                                       std::function<void(int)> chosen) {
    auto overlay = std::make_unique<Stack>();
    overlay->width = SizeSpec::fill();
    overlay->height = SizeSpec::fill();
    overlay->background = theme().scrim;
    overlay->horizontal = CrossAlign::Center;
    overlay->vertical = CrossAlign::Center;
    overlay->name = "choice dialog";
    auto* card = overlay->add<Box>(Axis::Column);
    card->width = SizeSpec::fixed(820);
    card->padding = Insets::all(theme().spacingXl);
    card->gap = theme().spacingL;
    card->background = theme().surfaceRaised;
    card->cornerRadius = theme().radius;
    auto* heading = card->add<Label>(std::move(title), FontStyle{theme().fontHeading, FontWeight::SemiBold});
    heading->width = SizeSpec::fill();
    auto* scroll = card->add<VScroll>();
    scroll->policy = VScroll::Policy::Nearest;
    scroll->nearestMargin = theme().spacingM;
    scroll->clip = true;
    auto list = std::make_unique<Box>(Axis::Column);
    list->width = SizeSpec::fill();
    list->gap = theme().spacingM;
    scroll->height = SizeSpec::fit();
    scroll->maxHeight = 640;  // about nine choices; more scroll
    auto shared = std::make_shared<std::function<void(int)>>(std::move(chosen));
    for (std::size_t i = 0; i < choices.size(); ++i) {
        Button* button = list->add<Button>(choices[i], [shared, i] {
            Application* app = Application::current();
            if (app != nullptr) {
                app->closeModal();
            }
            if (*shared) {
                (*shared)(static_cast<int>(i));
            }
        });
        button->width = SizeSpec::fill();
        if (static_cast<int>(i) == selected) {
            overlay->remembered = button;
        }
    }
    scroll->setContent(std::move(list));
    return overlay;
}

std::unique_ptr<View> makeDialog(std::string title, std::string message,
                                 std::vector<std::pair<std::string, std::function<void()>>> buttons) {
    auto overlay = std::make_unique<Stack>();
    overlay->width = SizeSpec::fill();
    overlay->height = SizeSpec::fill();
    overlay->background = theme().scrim;
    overlay->horizontal = CrossAlign::Center;
    overlay->vertical = CrossAlign::Center;
    overlay->name = "dialog";
    auto* card = overlay->add<Box>(Axis::Column);
    card->width = SizeSpec::fixed(820);
    card->padding = Insets::all(theme().spacingXl);
    card->gap = theme().spacingL;
    card->background = theme().surfaceRaised;
    card->cornerRadius = theme().radius;
    auto* heading = card->add<Label>(std::move(title), FontStyle{theme().fontHeading, FontWeight::SemiBold});
    heading->width = SizeSpec::fill();
    heading->maxLines = 2;
    auto* body = card->add<Label>(std::move(message), FontStyle{theme().fontBody, FontWeight::Regular});
    body->width = SizeSpec::fill();
    body->maxLines = 0;
    body->color = theme().textSecondary;
    // two buttons side by side; more become a list (a row of long labels would leave the card)
    const bool list = buttons.size() > 2;
    auto* row = card->add<Box>(list ? Axis::Column : Axis::Row);
    row->gap = theme().spacingM;
    if (list) {
        row->width = SizeSpec::fill();
    }
    for (auto& entry : buttons) {
        Button* button = row->add<Button>(entry.first, [action = entry.second] {
            Application* app = Application::current();
            if (app != nullptr) {
                app->closeModal();
            }
            if (action) {
                action();
            }
        });
        if (list) {
            button->width = SizeSpec::fill();
        }
    }
    return overlay;
}

}  // namespace playfin::ui
