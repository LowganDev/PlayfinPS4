#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <string>

#include "core/task.hpp"
#include "support/fixed_metrics.hpp"
#include "ui/application.hpp"
#include "ui/context.hpp"
#include "ui/box.hpp"
#include "ui/video_source.hpp"
#include "ui/reveal.hpp"
#include "ui/widgets.hpp"

using namespace playfin;
using platform::Action;
using platform::Event;

namespace {

Event press(Action action) {
    Event event;
    event.type = Event::Type::ActionDown;
    event.action = action;
    return event;
}

Event typed(const std::string& text) {
    Event event;
    event.type = Event::Type::Text;
    event.text = text;
    return event;
}

struct ButtonsScreen : ui::Screen {
    ui::Button* first = nullptr;
    ui::Button* second = nullptr;
    int presses = 0;
    ButtonsScreen() {
        auto* row = add<ui::Box>(ui::Axis::Row);
        first = row->add<ui::Button>("One", [this] { ++presses; });
        second = row->add<ui::Button>("Two", [this] { ++presses; });
    }
};

struct Rig {
    core::WorkerPool pool{1};
    core::MainQueue mainQueue;
    test::FixedMetrics metrics;
    std::unique_ptr<ui::Application> app;
    Rig() { app = std::make_unique<ui::Application>(ui::Application::Services{pool, mainQueue, metrics, nullptr}); }
    void frames(int count = 1) {
        for (int i = 0; i < count; ++i) {
            app->update(1000.0f / 60.0f);
        }
    }
};

}  // namespace

TEST_CASE("a pushed screen gets the focus; Confirm presses the focused button") {
    Rig rig;
    auto* screen = static_cast<ButtonsScreen*>(rig.app->push(std::make_unique<ButtonsScreen>()));
    rig.frames();
    CHECK(rig.app->focus().focused() == screen->first);
    rig.app->handle(press(Action::Right));
    CHECK(rig.app->focus().focused() == screen->second);
    rig.app->handle(press(Action::Confirm));
    CHECK(screen->presses == 1);
}

TEST_CASE("Back pops screens and restores the previous focus") {
    Rig rig;
    auto* home = static_cast<ButtonsScreen*>(rig.app->push(std::make_unique<ButtonsScreen>()));
    rig.frames();
    rig.app->handle(press(Action::Right));
    rig.app->push(std::make_unique<ButtonsScreen>());
    rig.frames();
    CHECK(rig.app->depth() == 2);
    rig.app->handle(press(Action::Back));
    rig.frames();
    CHECK(rig.app->depth() == 1);
    CHECK(rig.app->focus().focused() == home->second);
    rig.app->handle(press(Action::Back));  // the last screen stays
    CHECK(rig.app->depth() == 1);
}

TEST_CASE("a modal dialog keeps the focus until closed") {
    Rig rig;
    auto* home = static_cast<ButtonsScreen*>(rig.app->push(std::make_unique<ButtonsScreen>()));
    rig.frames();
    int confirmed = 0;
    rig.app->showModal(ui::makeDialog("Title", "Message", {{"OK", [&] { ++confirmed; }}, {"Cancel", [] {}}}));
    rig.frames();
    CHECK(rig.app->hasModal());
    rig.app->handle(press(Action::Down));
    rig.app->handle(press(Action::Up));
    CHECK_FALSE(home->isAncestorOf(rig.app->focus().focused()));
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    CHECK(confirmed == 1);
    CHECK_FALSE(rig.app->hasModal());
    CHECK(rig.app->focus().focused() == home->first);
    rig.app->showModal(ui::makeDialog("Title", "Message", {{"OK", [] {}}}));
    rig.frames();
    rig.app->handle(press(Action::Back));
    rig.frames();
    CHECK_FALSE(rig.app->hasModal());
}

TEST_CASE("the dialogs of a screen close when the screen goes, the ones below stay") {
    Rig rig;
    rig.app->push(std::make_unique<ButtonsScreen>());
    rig.app->showModal(ui::makeDialog("Below", "", {{"OK", [] {}}}));
    rig.app->closeModal();
    rig.app->push(std::make_unique<ButtonsScreen>());
    rig.app->showModal(ui::makeDialog("Player", "", {{"OK", [] {}}}));
    rig.app->pop();
    CHECK_FALSE(rig.app->hasModal());

    rig.app->showModal(ui::makeDialog("Kept", "", {{"OK", [] {}}}));
    rig.app->push(std::make_unique<ButtonsScreen>());
    rig.app->showModal(ui::makeDialog("Top", "", {{"OK", [] {}}}));
    rig.app->replaceTop(std::make_unique<ButtonsScreen>());
    CHECK(rig.app->hasModal());  // "Kept" belongs to the screen below
    rig.app->closeModal();
    CHECK_FALSE(rig.app->hasModal());

    rig.app->showModal(ui::makeDialog("Any", "", {{"OK", [] {}}}));
    rig.app->replace(std::make_unique<ButtonsScreen>());
    CHECK_FALSE(rig.app->hasModal());
}

TEST_CASE("closing the screens hides and destroys them all, with their dialogs") {
    struct Watched : ButtonsScreen {
        int* hidden;
        bool* destroyed;
        Watched(int* h, bool* d) : hidden(h), destroyed(d) {}
        ~Watched() override { *destroyed = true; }
        void onHide() override { ++*hidden; }
    };
    Rig rig;
    int hidden = 0;
    bool first = false;
    bool second = false;
    rig.app->push(std::make_unique<Watched>(&hidden, &first));
    rig.app->push(std::make_unique<Watched>(&hidden, &second));
    rig.app->showModal(ui::makeDialog("Open", "", {{"OK", [] {}}}));
    rig.app->closeScreens();
    CHECK(rig.app->depth() == 0);
    CHECK_FALSE(rig.app->hasModal());
    CHECK(first);
    CHECK(second);
    CHECK(hidden >= 2);
}

TEST_CASE("text fields edit with the PC keyboard on Linux") {
    Rig rig;
    struct FieldScreen : ui::Screen {
        ui::TextField* field = nullptr;
        std::string changed;
        FieldScreen() {
            platform::TextInputRequest request;
            request.title = "Name";
            field = add<ui::TextField>("Name", request, [this](const std::string& value) { changed = value; });
        }
    };
    auto* screen = static_cast<FieldScreen*>(rig.app->push(std::make_unique<FieldScreen>()));
    rig.frames();
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    CHECK(rig.app->hasModal());
    rig.app->handle(typed("Ana"));
    rig.app->handle(typed("\xC3\xA9"));
    Event erase;
    erase.type = Event::Type::EditDelete;
    rig.app->handle(erase);
    rig.app->handle(typed("s"));
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    CHECK_FALSE(rig.app->hasModal());
    CHECK(screen->field->value() == "Anas");
    CHECK(screen->changed == "Anas");
}

namespace {

int gFieldChanges = 0;

struct CountingFieldScreen : ui::Screen {
    ui::TextField* field = nullptr;
    CountingFieldScreen() {
        platform::TextInputRequest request;
        request.title = "Name";
        field = add<ui::TextField>("Name", request, [](const std::string&) { ++gFieldChanges; });
    }
};

}  // namespace

TEST_CASE("a text field destroyed while it is edited never gets the result") {
    Rig rig;
    gFieldChanges = 0;
    rig.app->push(std::make_unique<CountingFieldScreen>());
    rig.frames();
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    REQUIRE(rig.app->hasModal());
    rig.app->replace(std::make_unique<ButtonsScreen>());  // e.g. a network reply moves on
    rig.frames();
    rig.app->handle(typed("x"));
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    CHECK(gFieldChanges == 0);
    CHECK_FALSE(rig.app->hasModal());
}

TEST_CASE("an editor closed another way does not block the next one") {
    Rig rig;
    rig.app->push(std::make_unique<CountingFieldScreen>());
    rig.frames();
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    REQUIRE(rig.app->hasModal());
    rig.app->closeModal();
    rig.frames();
    REQUIRE_FALSE(rig.app->hasModal());
    rig.app->handle(press(Action::Confirm));
    rig.frames();
    CHECK(rig.app->hasModal());
}

TEST_CASE("dialog buttons stay inside the card, however many there are") {
    Rig rig;
    rig.app->push(std::make_unique<ButtonsScreen>());
    rig.app->showModal(ui::makeDialog("Menu", "Test Server",
                                      {{"Switch the current user", [] {}},
                                       {"Switch to another server", [] {}},
                                       {"Sign out of this server", [] {}},
                                       {"Cancel", [] {}}}));
    rig.frames(2);
    ui::View* overlay = rig.app->root().children().back().get();
    ui::View* card = overlay->children().front().get();
    const ui::Rect box = card->absoluteFrame();
    int buttons = 0;
    std::function<void(ui::View&)> visit = [&](ui::View& view) {
        if (dynamic_cast<ui::Button*>(&view) != nullptr) {
            ++buttons;
            const ui::Rect frame = view.absoluteFrame();
            CHECK(frame.x >= box.x);
            CHECK(frame.x + frame.width <= box.x + box.width);
        }
        for (const auto& child : view.children()) {
            visit(*child);
        }
    };
    visit(*card);
    CHECK(buttons == 4);
}

TEST_CASE("work posted to the main queue runs during update") {
    Rig rig;
    rig.app->push(std::make_unique<ButtonsScreen>());
    bool ran = false;
    rig.mainQueue.post([&] { ran = true; });
    rig.frames();
    CHECK(ran);
}

namespace {

// runs frames, drawing when asked, until the application settles (focus ring glide, first layout)
void settle(Rig& rig) {
    for (int i = 0; i < 120; ++i) {
        rig.app->update(1000.0f / 60.0f);
        if (rig.app->needsDraw()) {
            rig.app->markDrawn();
        }
    }
}

}  // namespace

TEST_CASE("an idle application stops asking to draw; input, jobs and spinners ask again") {
    Rig rig;
    rig.app->push(std::make_unique<ButtonsScreen>());
    settle(rig);
    rig.app->update(1000.0f / 60.0f);
    CHECK_FALSE(rig.app->needsDraw());

    rig.app->handle(press(Action::Right));
    rig.app->update(1000.0f / 60.0f);
    CHECK(rig.app->needsDraw());
    settle(rig);
    CHECK_FALSE(rig.app->needsDraw());

    rig.mainQueue.post([] {});
    rig.app->update(1000.0f / 60.0f);
    CHECK(rig.app->needsDraw());
    settle(rig);

    rig.app->toast("hello", 100);
    CHECK(rig.app->needsDraw());
    settle(rig);
    CHECK_FALSE(rig.app->needsDraw());

    ui::context().redrawNextFrame = true;  // what a spinner does while it is drawn
    rig.app->markDrawn();
    CHECK(rig.app->needsDraw());
    rig.app->markDrawn();
    CHECK_FALSE(rig.app->needsDraw());
}

namespace {
struct StillVideo : ui::VideoSource {
    bool renderVideo() override { return false; }
    platform::VideoLayer videoLayer() const override { return {}; }
};
}  // namespace

TEST_CASE("the application keeps the video source a screen registers") {
    Rig rig;
    StillVideo video;
    CHECK(rig.app->videoSource() == nullptr);
    rig.app->setVideoSource(&video);
    CHECK(rig.app->videoSource() == &video);
    CHECK(rig.app->root().background.a == 0);  // the video shows through
    rig.app->setVideoSource(nullptr);
    CHECK(rig.app->videoSource() == nullptr);
    CHECK(rig.app->root().background.a > 0);
}

TEST_CASE("the halo background shows under the screens, never under a video") {
    Rig rig;
    CHECK_FALSE(rig.app->backgroundShown());
    rig.app->setBackgroundImage(7, 1920, 1080);
    CHECK(rig.app->backgroundShown());
    StillVideo video;
    rig.app->setVideoSource(&video);
    CHECK_FALSE(rig.app->backgroundShown());
    CHECK(rig.app->clearColor().a == 0);
    rig.app->setVideoSource(nullptr);
    CHECK(rig.app->backgroundShown());
    CHECK(rig.app->clearColor().r == ui::theme().background.r);
    CHECK(rig.app->clearColor().a == 255);
}

TEST_CASE("a pushed screen fades in and rises; the screen revealed by pop fades back in") {
    Rig rig;
    ui::Screen* first = rig.app->push(std::make_unique<ButtonsScreen>());
    rig.frames(30);
    ui::Screen* second = rig.app->push(std::make_unique<ButtonsScreen>());
    CHECK(second->opacity == doctest::Approx(0));
    CHECK(second->shiftY == doctest::Approx(24));
    rig.frames(22);  // 367 ms
    CHECK(second->opacity == doctest::Approx(1));
    CHECK(second->shiftY == doctest::Approx(0));
    rig.app->pop();
    CHECK(first->opacity == doctest::Approx(0));
    rig.frames(8);  // 133 ms
    CHECK(first->opacity > 0.5f);
    CHECK(first->opacity < 1.0f);
    rig.frames(8);
    CHECK(first->opacity == doctest::Approx(1));
}

TEST_CASE("a replaced screen fades in without rising") {
    Rig rig;
    rig.app->push(std::make_unique<ButtonsScreen>());
    ui::Screen* tab = rig.app->replace(std::make_unique<ButtonsScreen>());
    CHECK(tab->opacity == doctest::Approx(0));
    CHECK(tab->shiftY == doctest::Approx(0));
    rig.frames(16);  // 267 ms
    CHECK(tab->opacity == doctest::Approx(1));
}

TEST_CASE("a screen can ask for a longer rise") {
    Rig rig;
    auto screen = std::make_unique<ButtonsScreen>();
    screen->enterRise = 40;
    ui::Screen* pushed = rig.app->push(std::move(screen));
    CHECK(pushed->shiftY == doctest::Approx(40));
}

TEST_CASE("without transitions screens show at once") {
    Rig rig;
    rig.app->setTransitions(false);
    CHECK_FALSE(rig.app->transitions());
    ui::Screen* screen = rig.app->push(std::make_unique<ButtonsScreen>());
    CHECK(screen->opacity == doctest::Approx(1));
    CHECK(screen->shiftY == doctest::Approx(0));
}

TEST_CASE("rows arrive one after the other, at most twelve of them animated") {
    Rig rig;
    auto* screen = rig.app->push(std::make_unique<ui::Screen>());
    auto* column = screen->add<ui::Box>(ui::Axis::Column);
    for (int i = 0; i < 15; ++i) {
        column->add<ui::Box>(ui::Axis::Row);
    }
    rig.frames(30);
    ui::revealChildren(*column, 1);
    const auto& rows = column->children();
    CHECK(rows[0]->opacity == doctest::Approx(1));  // before `first`: untouched
    CHECK(rows[1]->opacity == doctest::Approx(0));
    CHECK(rows[1]->shiftY == doctest::Approx(30));
    CHECK(rows[13]->opacity == doctest::Approx(1));  // the 13th revealed row shows at once
    rig.frames(3);  // 50 ms
    CHECK(rows[1]->opacity > 0);
    CHECK(rows[3]->opacity == doctest::Approx(0));  // starts 70 ms later
    rig.frames(42);  // 750 ms: the 12th started at 385 ms
    CHECK(rows[12]->opacity == doctest::Approx(1));
    CHECK(rows[12]->shiftY == doctest::Approx(0));
}

TEST_CASE("without transitions rows show at once") {
    Rig rig;
    rig.app->setTransitions(false);
    auto* screen = rig.app->push(std::make_unique<ui::Screen>());
    auto* row = screen->add<ui::Box>(ui::Axis::Row);
    ui::revealChildren(*screen, 0);
    CHECK(row->opacity == doctest::Approx(1));
}
