#include <doctest/doctest.h>

#include <vector>

#include "support/fixed_metrics.hpp"
#include "ui/box.hpp"
#include "ui/context.hpp"
#include "ui/focus.hpp"

using namespace playfin::ui;
using playfin::platform::Action;

namespace {

struct FocusFixture {
    playfin::test::FixedMetrics metrics;
    FocusManager focus;
    Box root{Axis::Column};
    std::vector<Box*> rows;
    std::vector<std::vector<View*>> items;

    FocusFixture() {
        context().focus = &focus;
        root.gap = 20;
        for (int count : {3, 5}) {
            Box* row = root.add<Box>(Axis::Row);
            row->gap = 20;
            rows.push_back(row);
            items.emplace_back();
            for (int i = 0; i < count; ++i) {
                View* item = row->add<View>();
                item->width = SizeSpec::fixed(100);
                item->height = SizeSpec::fixed(100);
                item->focusable = true;
                items.back().push_back(item);
            }
        }
        focus.setScope(&root);
        root.place({0, 0, 1000, 600}, metrics);
    }
    ~FocusFixture() {
        rows.clear();
        root.clearChildren();
        context().focus = nullptr;
    }
};

}  // namespace

TEST_CASE("focusing a container focuses its first focusable descendant") {
    FocusFixture f;
    CHECK(f.focus.focus(&f.root));
    CHECK(f.focus.focused() == f.items[0][0]);
}

TEST_CASE("left and right stay in the row and stop at its ends") {
    FocusFixture f;
    f.focus.focus(f.items[0][0]);
    CHECK(f.focus.move(Direction::Right));
    CHECK(f.focus.move(Direction::Right));
    CHECK(f.focus.focused() == f.items[0][2]);
    CHECK_FALSE(f.focus.move(Direction::Right));
    CHECK(f.focus.focused() == f.items[0][2]);
    CHECK(f.focus.move(Direction::Left));
    CHECK(f.focus.focused() == f.items[0][1]);
}

TEST_CASE("up and down pick the nearest item, also in rows of other lengths") {
    FocusFixture f;
    f.focus.focus(f.items[0][2]);
    CHECK(f.focus.move(Direction::Down));
    CHECK(f.focus.focused() == f.items[1][2]);
    f.focus.focus(f.items[1][4]);
    CHECK(f.focus.move(Direction::Up));
    CHECK(f.focus.focused() == f.items[0][2]);
    CHECK_FALSE(f.focus.move(Direction::Up));
}

TEST_CASE("a row that remembers focus is re-entered where it was left") {
    FocusFixture f;
    f.rows[1]->rememberFocus = true;
    f.focus.focus(f.items[1][4]);
    f.focus.move(Direction::Up);
    CHECK(f.focus.focused() == f.items[0][2]);
    f.focus.move(Direction::Left);
    f.focus.move(Direction::Left);
    CHECK(f.focus.move(Direction::Down));
    CHECK(f.focus.focused() == f.items[1][4]);
}

TEST_CASE("explicit overrides win") {
    FocusFixture f;
    f.items[0][0]->setNextFocus(Direction::Down, f.items[1][3]);
    f.focus.focus(f.items[0][0]);
    f.focus.move(Direction::Down);
    CHECK(f.focus.focused() == f.items[1][3]);
}

TEST_CASE("the scope keeps focus inside a modal") {
    FocusFixture f;
    f.focus.setScope(f.rows[0]);
    f.focus.focus(f.items[0][1]);
    CHECK_FALSE(f.focus.move(Direction::Down));
    CHECK(f.focus.focused() == f.items[0][1]);
}

TEST_CASE("focus changes notify the views") {
    struct Probe : View {
        int gained = 0;
        int lost = 0;
        void onFocusChanged(bool focused) override { (focused ? gained : lost)++; }
    };
    FocusFixture f;
    Probe* probe = f.rows[0]->add<Probe>();
    probe->width = SizeSpec::fixed(50);
    probe->height = SizeSpec::fixed(50);
    probe->focusable = true;
    f.focus.focus(probe);
    f.focus.focus(f.items[0][0]);
    CHECK(probe->gained == 1);
    CHECK(probe->lost == 1);
}

TEST_CASE("destroying the focused view clears the focus") {
    FocusFixture f;
    f.focus.focus(f.items[1][3]);
    auto row = f.root.takeChild(f.rows[1]);
    row.reset();
    CHECK(f.focus.focused() == nullptr);
}

TEST_CASE("actions run the innermost binding; directions fall back to moving") {
    FocusFixture f;
    int rowConfirm = 0;
    int itemConfirm = 0;
    f.rows[0]->bind(Action::Confirm, "Open", [&] {
        ++rowConfirm;
        return true;
    });
    f.items[0][1]->bind(Action::Confirm, "Play", [&] {
        ++itemConfirm;
        return true;
    });
    f.focus.focus(f.items[0][0]);
    CHECK(f.focus.dispatch(Action::Confirm));
    CHECK(rowConfirm == 1);
    CHECK(f.focus.dispatch(Action::Right));
    CHECK(f.focus.dispatch(Action::Confirm));
    CHECK(itemConfirm == 1);
    CHECK(rowConfirm == 1);
    CHECK_FALSE(f.focus.dispatch(Action::Back));
    const auto chain = f.focus.actionChain();
    REQUIRE(chain.size() == 2);
    CHECK(chain[0]->label == "Play");
}

TEST_CASE("an explicit next focus that was destroyed is ignored") {
    FocusFixture f;
    REQUIRE(f.focus.focus(f.items[0][0]));
    f.items[0][0]->setNextFocus(Direction::Down, f.items[1][3]);
    f.rows[1]->takeChild(f.items[1][3]).reset();
    f.items[1].erase(f.items[1].begin() + 3);
    CHECK(f.focus.move(Direction::Down));  // the geometric neighbour instead
    CHECK(f.focus.focused() == f.items[1][0]);
}
