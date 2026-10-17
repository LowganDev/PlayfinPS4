#include "ui/application.hpp"
#include "ui/text.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>

#include "core/utf.hpp"
#include "ui/context.hpp"
#include "ui/draw.hpp"
#include "ui/halo.hpp"
#include "ui/hints.hpp"
#include "ui/i18n.hpp"
#include "ui/label.hpp"

namespace playfin::ui {

namespace {

Application* gCurrent = nullptr;

}  // namespace

Screen::Screen() {
    width = SizeSpec::fill();
    height = SizeSpec::fill();
    rememberFocus = true;
}

Application::Application(Services services, Size screen) : services_(services), screen_(screen) {
    gCurrent = this;
    Context& ctx = context();
    ctx.animator = &animator_;
    ctx.focus = &focus_;
    ctx.images = services_.images;
    ctx.metrics = &services_.metrics;
    root_ = std::make_unique<Stack>();
    root_->background = theme().background;
    auto layer = std::make_unique<Stack>();
    layer->width = SizeSpec::fill();
    layer->height = SizeSpec::fill();
    screenLayer_ = static_cast<Stack*>(root_->addChild(std::move(layer)));
}

Application::~Application() {
    root_.reset();
    Context& ctx = context();
    ctx.animator = nullptr;
    ctx.focus = nullptr;
    ctx.images = nullptr;
    ctx.metrics = nullptr;
    if (gCurrent == this) {
        gCurrent = nullptr;
    }
}

Application* Application::current() { return gCurrent; }

void Application::setVideoSource(VideoSource* source) {
    videoSource_ = source;
    if (root_) {  // null while the application is being destroyed (the player unregisters then)
        root_->background = source != nullptr || backgroundImage_ > 0 ? Color{0, 0, 0, 0} : theme().background;
    }
    redraw_ = true;
}

void Application::setBackgroundImage(int imageId, int width, int height) {
    backgroundImage_ = imageId;
    backgroundWidth_ = width;
    backgroundHeight_ = height;
    root_->background = imageId > 0 || videoSource_ != nullptr ? Color{0, 0, 0, 0} : theme().background;
    redraw_ = true;
}

Color Application::clearColor() const {
    if (videoSource_ != nullptr) {
        return {0, 0, 0, 0};
    }
    return backgroundImage_ > 0 ? theme().background : Color{0, 0, 0, 255};
}

Screen* Application::top() const { return screens_.empty() ? nullptr : screens_.back(); }

Screen* Application::push(std::unique_ptr<Screen> screen) {
    if (Screen* previous = top()) {
        previous->onHide();
        previous->visible = false;
    }
    Screen* raw = static_cast<Screen*>(screenLayer_->addChild(std::move(screen)));
    screens_.push_back(raw);
    enter(raw, theme().pushMs, raw->enterRise);
    raw->onShow();
    refocus();
    return raw;
}

void Application::enter(Screen* screen, float durationMs, float rise) {
    animator_.cancel(screen);
    if (!transitions_) {
        screen->opacity = 1;
        screen->shiftY = 0;
        return;
    }
    screen->opacity = 0;
    screen->shiftY = rise;
    animator_.animate(&screen->opacity, 1, durationMs, Easing::Smooth, screen);
    if (rise != 0) {
        animator_.animate(&screen->shiftY, 0, durationMs, Easing::Smooth, screen);
    }
}

void Application::pop() {
    if (screens_.size() <= 1) {
        return;
    }
    Screen* leaving = screens_.back();
    closeModalsOf(leaving);
    leaving->onHide();
    screens_.pop_back();
    screenLayer_->takeChild(leaving).reset();
    Screen* back = top();
    back->visible = true;
    back->markDirty();
    enter(back, theme().popMs, 0);
    back->onShow();
    refocus();
}

Screen* Application::replace(std::unique_ptr<Screen> screen) {
    closeScreens();
    Screen* raw = push(std::move(screen));
    enter(raw, theme().popMs, 0);
    return raw;
}

void Application::closeScreens() {
    while (!modals_.empty()) {
        closeModal();
    }
    if (Screen* leaving = top()) {
        leaving->onHide();
    }
    while (!screens_.empty()) {
        Screen* leaving = screens_.back();
        screens_.pop_back();
        screenLayer_->takeChild(leaving).reset();
    }
}

Screen* Application::replaceTop(std::unique_ptr<Screen> screen) {
    if (!screens_.empty()) {
        Screen* leaving = screens_.back();
        closeModalsOf(leaving);
        leaving->onHide();
        screens_.pop_back();
        screenLayer_->takeChild(leaving).reset();
    }
    Screen* raw = static_cast<Screen*>(screenLayer_->addChild(std::move(screen)));
    screens_.push_back(raw);
    enter(raw, theme().popMs, 0);
    raw->onShow();
    refocus();
    return raw;
}

void Application::showModal(std::unique_ptr<View> modal) {
    modal->width = SizeSpec::fill();
    modal->height = SizeSpec::fill();
    modals_.push_back({root_->addChild(std::move(modal)), top()});
    refocus();
}

void Application::closeModal() {
    if (modals_.empty()) {
        return;
    }
    View* modal = modals_.back().view;
    modals_.pop_back();
    if (modal == editor_) {
        editor_ = nullptr;
        editLabel_ = nullptr;
        if (!systemKeyboard_ && textDone_) {  // closed without finishEditing: the request ends, cancelled
            platform::setTextEvents(false);
            auto done = std::move(textDone_);
            textDone_ = nullptr;
            done(std::nullopt);
        }
    }
    root_->takeChild(modal).reset();
    refocus();
}

void Application::closeModalsOf(const Screen* screen) {
    while (!modals_.empty() && modals_.back().owner == screen) {
        closeModal();
    }
}

void Application::refocus() {
    redraw_ = true;
    View* scope = !modals_.empty() ? modals_.back().view : static_cast<View*>(top());
    focus_.setScope(scope);
    if (scope != nullptr) {
        root_->place({0, 0, screen_.width, screen_.height}, services_.metrics);
        focus_.focus(scope);
    }
}

void Application::toast(std::string text, float durationMs) {
    toastText_ = std::move(text);
    toastLeftMs_ = durationMs;
    redraw_ = true;
}

void Application::requestText(const platform::TextInputRequest& request,
                              std::function<void(std::optional<std::string>)> done) {
    if (textDone_) {
        return;  // one editor at a time
    }
    textDone_ = std::move(done);
    if (platform::systemKeyboardAvailable() && platform::openSystemKeyboard(request)) {
        systemKeyboard_ = true;
        return;
    }
    systemKeyboard_ = false;
    editText_ = request.initial;
    auto overlay = std::make_unique<Stack>();
    overlay->background = theme().scrim;
    overlay->horizontal = CrossAlign::Center;
    overlay->vertical = CrossAlign::Center;
    overlay->name = "editor";
    auto* card = overlay->add<Box>(Axis::Column);
    card->width = SizeSpec::fixed(900);
    card->padding = Insets::all(theme().spacingXl);
    card->gap = theme().spacingM;
    card->background = theme().surface;
    card->cornerRadius = theme().radius * 2;
    card->focusable = true;
    card->add<Label>(request.title, FontStyle{theme().fontTitle, FontWeight::SemiBold});
    editLabel_ = card->add<Label>(editText_ + "|", FontStyle{theme().fontHeading, FontWeight::Regular});
    editLabel_->width = SizeSpec::fill();
    card->bind(platform::Action::Confirm, tr("ui.done"), [this] {
        finishEditing(true);
        return true;
    });
    card->bind(platform::Action::Back, tr("ui.cancel"), [this] {
        finishEditing(false);
        return true;
    });
    editor_ = overlay.get();
    showModal(std::move(overlay));
    platform::setTextEvents(true);
}

void Application::finishEditing(bool accepted) {
    platform::setTextEvents(false);
    auto done = std::move(textDone_);
    textDone_ = nullptr;
    const std::string text = editText_;
    if (editor_ != nullptr) {
        while (!modals_.empty() && modals_.back().view != editor_) {
            closeModal();
        }
        closeModal();
    }
    if (done) {
        done(accepted ? std::optional<std::string>(text) : std::nullopt);
    }
}

void Application::handle(const platform::Event& event) {
    redraw_ = true;
    if (editor_ != nullptr && event.type == platform::Event::Type::Text) {
        editText_ += event.text;
        editLabel_->setText(editText_ + "|");
        return;
    }
    if (editor_ != nullptr && event.type == platform::Event::Type::EditDelete) {
        editText_.resize(core::previousCodePointStart(editText_, editText_.size()));
        editLabel_->setText(editText_ + "|");
        return;
    }
    if (event.type != platform::Event::Type::ActionDown) {
        return;
    }
    if (focus_.dispatch(event.action)) {
        return;
    }
    if (event.action == platform::Action::Back) {
        if (!modals_.empty()) {
            closeModal();
        } else {
            pop();
        }
    }
}

void Application::update(float dtMs) {
    Context& ctx = context();
    ctx.timeMs += dtMs;
    if (themeGeneration_ != themeGeneration()) {
        themeGeneration_ = themeGeneration();
        root_->onThemeChanged();
        redraw_ = true;
    }
    if (services_.mainQueue.drain() > 0) {
        redraw_ = true;
    }
    if (animator_.active()) {
        redraw_ = true;  // includes the tick that ends an animation
    }
    animator_.tick(dtMs);
    if (services_.images != nullptr && services_.images->uploadPending() > 0) {
        redraw_ = true;
    }
    if (toastLeftMs_ > 0) {
        toastLeftMs_ -= dtMs;
        redraw_ = true;  // includes the frame that removes it
    }
    if (textDone_ && systemKeyboard_) {
        std::string text;
        const platform::KeyboardStatus status = platform::pollSystemKeyboard(text);
        if (status == platform::KeyboardStatus::Done || status == platform::KeyboardStatus::Cancelled ||
            status == platform::KeyboardStatus::Failed) {
            auto done = std::move(textDone_);
            textDone_ = nullptr;
            systemKeyboard_ = false;
            done(status == platform::KeyboardStatus::Done ? std::optional<std::string>(text) : std::nullopt);
        }
    }
    if (root_->needsLayout() || animator_.active()) {
        redraw_ = true;
        root_->place({0, 0, screen_.width, screen_.height}, services_.metrics);
    }
    // focus ring: glides from the previous focus to the new one
    View* focused = focus_.focused();
    if (focused != ringView_) {
        ringFrom_ = ringView_ != nullptr ? ring_ : (focused != nullptr ? focused->absoluteFrame() : Rect{});
        ringView_ = focused;
        ringProgress_ = 0;
        redraw_ = true;
    }
    if (focused != nullptr) {
        if (ringProgress_ < 1.0f) {
            redraw_ = true;
        }
        ringProgress_ = std::min(1.0f, ringProgress_ + dtMs / theme().focusMs);
        ring_ = lerp(ringFrom_, focused->absoluteFrame(), ease(Easing::EaseOutCubic, ringProgress_));
    }
}

void Application::markDrawn() {
    Context& ctx = context();
    redraw_ = ctx.redrawNextFrame;
    ctx.redrawNextFrame = false;
}

void Application::draw(NVGcontext* vg) const {
    if (backgroundShown()) {
        // only the lit rows: the frame is cleared to the background colour (clearColor()) below them
        const NVGpaint paint = nvgImagePattern(vg, 0, 0, static_cast<float>(backgroundWidth_),
                                               static_cast<float>(backgroundHeight_), 0, backgroundImage_, 1);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, static_cast<float>(backgroundWidth_), static_cast<float>(haloLitRows(backgroundHeight_)));
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }
    root_->draw(vg);
    if (ringView_ != nullptr && !ringView_->drawsOwnFocus && ringView_ != editor_ &&
        !(editor_ != nullptr && editor_->isAncestorOf(ringView_))) {
        const float grow = theme().focusRingWidth / 2 + 2;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ring_.x - grow, ring_.y - grow, ring_.width + 2 * grow, ring_.height + 2 * grow,
                       ringView_->cornerRadius + grow);
        nvgStrokeColor(vg, nvgColor(theme().focusRing));
        nvgStrokeWidth(vg, theme().focusRingWidth);
        nvgStroke(vg);
    }
    if (toastLeftMs_ > 0 && !toastText_.empty()) {
        nvgFontFace(vg, "regular");
        nvgFontSize(vg, theme().fontBody);
        float bounds[4];
        const float textWidth = nvgTextBounds(vg, 0, 0, toastText_.c_str(), nullptr, bounds);
        const float w = textWidth + 2 * theme().spacingL;
        const float h = theme().fontBody + 2 * theme().spacingM;
        const float x = (screen_.width - w) / 2;
        const float y = screen_.height - 160;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, h / 2);
        nvgFillColor(vg, nvgColor(theme().surfaceFocused));
        nvgFill(vg);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgColor(theme().text));
        nvgText(vg, screen_.width / 2, y + h / 2 + middleAlignedShift(theme().fontBody), toastText_.c_str(), nullptr);
    }
    drawHints(vg, focus_.actionChain(), screen_.width, screen_.height);
}

}  // namespace playfin::ui
