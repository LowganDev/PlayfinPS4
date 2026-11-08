#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui/box.hpp"
#include "ui/hints.hpp"
#include "ui/i18n.hpp"

using namespace playfin::ui;
using playfin::platform::Action;

namespace {

std::vector<const ActionBinding*> chainOf(const std::vector<ActionBinding>& bindings) {
    std::vector<const ActionBinding*> chain;
    for (const ActionBinding& binding : bindings) {
        chain.push_back(&binding);
    }
    return chain;
}

ActionBinding make(Action action, std::string label, std::function<bool()> shown = {}) {
    return ActionBinding{action, std::move(label), [] { return true; }, std::move(shown)};
}

const std::vector<Action> kPad = {Action::Up, Action::Down, Action::Left, Action::Right};

}  // namespace

TEST_CASE("hints: navigate first, the screen's actions in a fixed order, back last") {
    const std::vector<ActionBinding> bindings = {make(Action::Back, ""), make(Action::Options, "Menu"),
                                                 make(Action::Confirm, "Select")};
    std::vector<ActionBinding> noBack = {bindings[1], bindings[2]};
    const auto items = hintItems(chainOf(noBack), true, true);
    REQUIRE(items.size() == 4);
    CHECK(items[0] == HintItem{kPad, tr("hints.navigate")});
    CHECK(items[1] == HintItem{{Action::Confirm}, "Select"});
    CHECK(items[2] == HintItem{{Action::Options}, "Menu"});
    CHECK(items[3] == HintItem{{Action::Back}, tr("hints.back")});
}

TEST_CASE("hints: nothing to go back to, nothing to move to: neither is shown") {
    const std::vector<ActionBinding> bindings = {make(Action::Confirm, "Select")};
    const auto items = hintItems(chainOf(bindings), false, false);
    REQUIRE(items.size() == 1);
    CHECK(items[0] == HintItem{{Action::Confirm}, "Select"});
}

TEST_CASE("hints: the innermost binding of an action wins; unlabelled ones show nothing") {
    const std::vector<ActionBinding> bindings = {make(Action::Confirm, "Play now"), make(Action::Confirm, "Select"),
                                                 make(Action::Context, "")};
    const auto items = hintItems(chainOf(bindings), false, false);
    REQUIRE(items.size() == 1);
    CHECK(items[0] == HintItem{{Action::Confirm}, "Play now"});
}

TEST_CASE("hints: a pair of shoulder buttons shows once when one of them is labelled, twice when both are") {
    const std::vector<ActionBinding> filter = {make(Action::L2, ""), make(Action::R2, "Filter")};
    auto items = hintItems(chainOf(filter), false, false);
    REQUIRE(items.size() == 1);
    CHECK(items[0] == HintItem{{Action::L2, Action::R2}, "Filter"});
    const std::vector<ActionBinding> tracks = {make(Action::R1, "Next"), make(Action::L1, "Previous")};
    items = hintItems(chainOf(tracks), false, false);
    REQUIRE(items.size() == 2);
    CHECK(items[0] == HintItem{{Action::L1}, "Previous"});
    CHECK(items[1] == HintItem{{Action::R1}, "Next"});
}

TEST_CASE("hints: directions with a role of their own show their arrows, paired when they share a label") {
    const std::vector<ActionBinding> seek = {make(Action::Left, "10 s"), make(Action::Right, "10 s"),
                                             make(Action::Up, "Volume"), make(Action::Down, "Volume")};
    auto items = hintItems(chainOf(seek), false, false);
    REQUIRE(items.size() == 2);
    CHECK(items[0] == HintItem{{Action::Up, Action::Down}, "Volume"});
    CHECK(items[1] == HintItem{{Action::Left, Action::Right}, "10 s"});
    const std::vector<ActionBinding> jump = {make(Action::Left, "A–Z")};
    items = hintItems(chainOf(jump), true, false);
    REQUIRE(items.size() == 2);
    CHECK(items[0] == HintItem{kPad, tr("hints.navigate")});
    CHECK(items[1] == HintItem{{Action::Left}, "A–Z"});
}

TEST_CASE("hints: a screen's own back replaces the general one, shown or not") {
    bool barShown = false;
    const std::vector<ActionBinding> player = {make(Action::Back, "Stop", [&barShown] { return barShown; }),
                                               make(Action::Confirm, "Pause", [&barShown] { return barShown; })};
    CHECK(hintItems(chainOf(player), false, true).empty());
    barShown = true;
    const auto items = hintItems(chainOf(player), false, true);
    REQUIRE(items.size() == 2);
    CHECK(items[0] == HintItem{{Action::Confirm}, "Pause"});
    CHECK(items[1] == HintItem{{Action::Back}, "Stop"});
}

TEST_CASE("hints: of two bindings of one button, the label shown is the first whose label shows") {
    bool paused = false;
    const std::vector<ActionBinding> player = {make(Action::Confirm, "Pause", [&paused] { return !paused; }),
                                               make(Action::Confirm, "Play", [&paused] { return paused; })};
    CHECK(hintItems(chainOf(player), false, false).front() == HintItem{{Action::Confirm}, "Pause"});
    paused = true;
    CHECK(hintItems(chainOf(player), false, false).front() == HintItem{{Action::Confirm}, "Play"});
}

TEST_CASE("hints: navigating needs two views that can take the focus, and a scope that allows the hint") {
    Box root{Axis::Column};
    View* first = root.add<View>();
    first->focusable = true;
    CHECK_FALSE(navigable(&root));
    View* second = root.add<View>();
    second->focusable = true;
    CHECK(navigable(&root));
    second->visible = false;
    CHECK_FALSE(navigable(&root));
    second->visible = true;
    root.navigationHint = false;
    CHECK_FALSE(navigable(&root));
}
