#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/task.hpp"
#include "support/fixed_metrics.hpp"
#include "ui/application.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

using namespace playfin;
using namespace playfin::ui;
using playfin::platform::Action;
using playfin::platform::Event;

namespace {

bool same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

const Label* labelOf(const View& view) {
    for (const auto& child : view.children()) {
        if (const auto* label = dynamic_cast<const Label*>(child.get())) {
            return label;
        }
    }
    return nullptr;
}

struct Rig {
    core::WorkerPool pool{1};
    core::MainQueue mainQueue;
    test::FixedMetrics metrics;
    std::unique_ptr<Application> app;
    Rig() { app = std::make_unique<Application>(Application::Services{pool, mainQueue, metrics, nullptr}); }
    ~Rig() { applyAccent(accentPalette().front()); }
};

struct TwoButtons : Screen {
    Button* first;
    Button* second;
    TwoButtons() {
        auto* row = add<Box>(Axis::Row);
        first = row->add<Button>("Lire", [] {});
        second = row->add<Button>("Lire depuis le début", [] {});
    }
};

}  // namespace

TEST_CASE("a focused button fills with the accent, the others stay on the surface") {
    Rig rig;
    auto* screen = static_cast<TwoButtons*>(rig.app->push(std::make_unique<TwoButtons>()));
    rig.app->update(16);
    CHECK(same(screen->first->background, theme().accent));
    CHECK(same(labelOf(*screen->first)->color, theme().onAccent));
    CHECK(labelOf(*screen->first)->style.weight == FontWeight::SemiBold);
    CHECK(same(screen->second->background, theme().control));
    CHECK(same(labelOf(*screen->second)->color, theme().text));
    CHECK(screen->first->drawsOwnFocus);
}

TEST_CASE("a primary button keeps the accent without the focus") {
    Rig rig;
    auto* screen = static_cast<TwoButtons*>(rig.app->push(std::make_unique<TwoButtons>()));
    screen->second->setPrimary(true);
    rig.app->update(16);
    CHECK(same(screen->second->background, theme().accent));
}

TEST_CASE("changing the accent repaints the focused button on the next frame") {
    Rig rig;
    auto* screen = static_cast<TwoButtons*>(rig.app->push(std::make_unique<TwoButtons>()));
    rig.app->update(16);
    applyAccent(accentPalette()[3]);
    rig.app->update(16);
    CHECK(same(screen->first->background, accentPalette()[3].accent));
}

TEST_CASE("dialogs are raised cards") {
    auto dialog = makeDialog("Titre", "Message", {{"OK", {}}});
    REQUIRE_FALSE(dialog->children().empty());
    CHECK(same(dialog->children().front()->background, theme().surfaceRaised));
    CHECK(dialog->children().front()->cornerRadius == doctest::Approx(theme().radius));
}

namespace {
struct Rows : Screen {
    ValueRow* first;
    ValueRow* second;
    int presses = 0;
    Rows() {
        auto* column = add<Box>(Axis::Column);
        first = column->add<ValueRow>("Accent colour", "PlayStation blue", [this] { ++presses; });
        second = column->add<ValueRow>("Language", "System", [this] { ++presses; });
    }
};

}  // namespace

TEST_CASE("a value row shows its label and value, fills with the accent when focused and presses") {
    Rig rig;
    auto* screen = static_cast<Rows*>(rig.app->push(std::make_unique<Rows>()));
    rig.app->update(16);
    CHECK(same(screen->first->background, theme().accent));
    CHECK(same(screen->second->background, theme().control));
    CHECK(screen->first->value() == "PlayStation blue");
    CHECK(screen->first->drawsOwnFocus);
    screen->first->setValue("Red");
    CHECK(screen->first->value() == "Red");
    Event confirm;
    confirm.type = Event::Type::ActionDown;
    confirm.action = Action::Confirm;
    rig.app->handle(confirm);
    CHECK(screen->presses == 1);
}

TEST_CASE("a choice dialog focuses the current choice and reports the one chosen") {
    Rig rig;
    rig.app->push(std::make_unique<Screen>());
    int chosen = -1;
    std::vector<std::string> choices;
    for (int i = 0; i < 15; ++i) {
        choices.push_back("Choice " + std::to_string(i));
    }
    rig.app->showModal(makeChoiceDialog("Language", choices, 9, [&](int index) { chosen = index; }));
    rig.app->update(16);
    auto* focused = dynamic_cast<Button*>(rig.app->focus().focused());
    REQUIRE(focused != nullptr);
    CHECK(labelOf(*focused)->text() == "Choice 9");
    Event down;
    down.type = Event::Type::ActionDown;
    down.action = Action::Down;
    rig.app->handle(down);
    rig.app->update(16);
    Event confirm;
    confirm.type = Event::Type::ActionDown;
    confirm.action = Action::Confirm;
    rig.app->handle(confirm);
    rig.app->update(16);
    CHECK(chosen == 10);
    CHECK_FALSE(rig.app->hasModal());
}
