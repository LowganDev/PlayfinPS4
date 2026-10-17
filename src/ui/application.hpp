#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/task.hpp"
#include "platform/events.hpp"
#include "platform/text_input.hpp"
#include "ui/animation.hpp"
#include "ui/box.hpp"
#include "ui/focus.hpp"
#include "ui/images.hpp"
#include "ui/video_source.hpp"

namespace playfin::ui {

class Screen : public Stack {
public:
    Screen();
    virtual void onShow() {}
    virtual void onHide() {}
    float enterRise = 24;  // how far below its place a pushed screen starts (px)
};

// Owns the screens (a stack), the modals, focus, animations and the frame logic. One per process.
class Application {
public:
    struct Services {
        core::WorkerPool& pool;
        core::MainQueue& mainQueue;
        TextMetrics& metrics;
        ImageLoader* images;  // may be null (tests)
    };

    explicit Application(Services services, Size screen = {1920, 1080});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    static Application* current();

    // push: the screen fades in and rises (350 ms); pop: the revealed screen fades in (250 ms); replace: the new
    // screen fades in (250 ms). Not when transitions are off (script tests).
    Screen* push(std::unique_ptr<Screen> screen);
    void pop();
    Screen* replace(std::unique_ptr<Screen> screen);
    // The top screen gives way to `screen` (fades in like a replaced screen); the screens below stay.
    Screen* replaceTop(std::unique_ptr<Screen> screen);
    // At exit: every screen leaves (the player sends its last report), newest first.
    void closeScreens();
    void setTransitions(bool on) { transitions_ = on; }
    bool transitions() const { return transitions_; }
    Screen* top() const;
    std::size_t depth() const { return screens_.size(); }

    // A modal belongs to the screen on top when it opens and closes when that screen goes.
    void showModal(std::unique_ptr<View> modal);
    void closeModal();
    bool hasModal() const { return !modals_.empty(); }

    void toast(std::string text, float durationMs = 3000);
    void requestText(const platform::TextInputRequest& request, std::function<void(std::optional<std::string>)> done);

    void handle(const platform::Event& event);
    void update(float dtMs);
    void draw(NVGcontext* vg) const;
    // Whether the next frame differs from the last one drawn (input, layout, jobs, images, animations, toast,
    // focus ring, or a view that asked through Context::redrawNextFrame). markDrawn after drawing.
    bool needsDraw() const { return redraw_; }
    // Something outside the views changed what they show (the music's position): draw the next frame.
    void requestRedraw() { redraw_ = true; }
    void markDrawn();

    // An image drawn 1:1 under the screens (the halo); not while a video shows.
    void setBackgroundImage(int imageId, int width, int height);
    bool backgroundShown() const { return backgroundImage_ > 0 && videoSource_ == nullptr; }
    // What the frame is cleared to before drawing: the background colour under the halo, transparent under a
    // video, black otherwise (the root paints its own background).
    Color clearColor() const;

    // The player registers itself while it shows a video (nullptr: no video); the root's background is then
    // transparent.
    void setVideoSource(VideoSource* source);
    VideoSource* videoSource() const { return videoSource_; }

    FocusManager& focus() { return focus_; }
    Animator& animator() { return animator_; }
    View& root() { return *root_; }
    core::WorkerPool& pool() { return services_.pool; }
    core::MainQueue& mainQueue() { return services_.mainQueue; }

private:
    void refocus();
    void closeModalsOf(const Screen* screen);
    void enter(Screen* screen, float durationMs, float rise);
    void finishEditing(bool accepted);

    Services services_;
    Size screen_;
    Animator animator_;
    FocusManager focus_;
    std::unique_ptr<Stack> root_;
    Stack* screenLayer_ = nullptr;
    std::vector<Screen*> screens_;
    struct OpenModal {
        View* view;
        const Screen* owner;
    };
    std::vector<OpenModal> modals_;

    std::string toastText_;
    float toastLeftMs_ = 0;

    // text input
    std::function<void(std::optional<std::string>)> textDone_;
    bool systemKeyboard_ = false;
    std::string editText_;
    View* editor_ = nullptr;
    class Label* editLabel_ = nullptr;

    // focus ring
    View* ringView_ = nullptr;
    Rect ringFrom_;
    Rect ring_;
    float ringProgress_ = 1;
    bool redraw_ = true;
    bool transitions_ = true;
    VideoSource* videoSource_ = nullptr;
    unsigned themeGeneration_ = themeGeneration();
    int backgroundImage_ = 0;
    int backgroundWidth_ = 0;
    int backgroundHeight_ = 0;
};

}  // namespace playfin::ui
