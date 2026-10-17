#include <doctest/doctest.h>

#include "support/fixed_metrics.hpp"
#include "ui/box.hpp"
#include "ui/label.hpp"

using namespace playfin::ui;
using playfin::test::FixedMetrics;

namespace {

View* fixedChild(View& parent, float w, float h) {
    View* child = parent.add<View>();
    child->width = SizeSpec::fixed(w);
    child->height = SizeSpec::fixed(h);
    return child;
}

}  // namespace

TEST_CASE("a row places fixed children with padding and gap") {
    FixedMetrics metrics;
    Box row(Axis::Row);
    row.padding = Insets::all(10);
    row.gap = 5;
    View* a = fixedChild(row, 100, 50);
    View* b = fixedChild(row, 60, 30);
    const Size natural = row.measure({1000, 1000}, metrics);
    CHECK(natural.width == doctest::Approx(10 + 100 + 5 + 60 + 10));
    CHECK(natural.height == doctest::Approx(10 + 50 + 10));
    row.place({0, 0, 400, 200}, metrics);
    CHECK(a->frame.x == doctest::Approx(10));
    CHECK(a->frame.y == doctest::Approx(10));
    CHECK(b->frame.x == doctest::Approx(115));
    CHECK(b->frame.width == doctest::Approx(60));
}

TEST_CASE("fill children share the remaining space by weight") {
    FixedMetrics metrics;
    Box row(Axis::Row);
    fixedChild(row, 100, 10);
    View* one = row.add<View>();
    one->width = SizeSpec::fill(1);
    View* two = row.add<View>();
    two->width = SizeSpec::fill(2);
    row.place({0, 0, 400, 100}, metrics);
    CHECK(one->frame.x == doctest::Approx(100));
    CHECK(one->frame.width == doctest::Approx(100));
    CHECK(two->frame.x == doctest::Approx(200));
    CHECK(two->frame.width == doctest::Approx(200));
}

TEST_CASE("alignment on both axes") {
    FixedMetrics metrics;
    Box column(Axis::Column);
    column.align = Align::Center;
    column.crossAlign = CrossAlign::End;
    View* child = fixedChild(column, 50, 20);
    column.place({0, 0, 200, 100}, metrics);
    CHECK(child->frame.x == doctest::Approx(150));
    CHECK(child->frame.y == doctest::Approx(40));
    Box spread(Axis::Row);
    spread.align = Align::SpaceBetween;
    spread.crossAlign = CrossAlign::Stretch;
    View* left = fixedChild(spread, 20, 10);
    View* right = fixedChild(spread, 20, 10);
    right->height = SizeSpec::fit();
    spread.place({0, 0, 100, 30}, metrics);
    CHECK(left->frame.x == doctest::Approx(0));
    CHECK(right->frame.x == doctest::Approx(80));
    CHECK(right->frame.height == doctest::Approx(30));
}

TEST_CASE("margins and hidden children") {
    FixedMetrics metrics;
    Box column(Axis::Column);
    View* hidden = fixedChild(column, 10, 10);
    hidden->visible = false;
    View* shown = fixedChild(column, 10, 10);
    shown->margin = {5, 0, 0, 7};
    CHECK(column.measure({100, 100}, metrics).height == doctest::Approx(15));
    column.place({0, 0, 100, 100}, metrics);
    CHECK(shown->frame.y == doctest::Approx(5));
    CHECK(shown->frame.x == doctest::Approx(7));
}

TEST_CASE("absolute frames add parents and content offsets") {
    FixedMetrics metrics;
    Box outer(Axis::Column);
    outer.padding = Insets::all(20);
    Box* inner = outer.add<Box>(Axis::Row);
    inner->padding = Insets::all(5);
    View* leaf = fixedChild(*inner, 10, 10);
    outer.place({100, 50, 300, 300}, metrics);
    const Rect abs = leaf->absoluteFrame();
    CHECK(abs.x == doctest::Approx(100 + 20 + 5));
    CHECK(abs.y == doctest::Approx(50 + 20 + 5));
}

TEST_CASE("labels wrap to their width and report their height") {
    FixedMetrics metrics;
    Label label("aa bb cc dd ee", FontStyle{10, FontWeight::Regular});
    label.maxLines = 0;
    const Size natural = label.measure({30, 1000}, metrics);
    CHECK(natural.height == doctest::Approx(3 * 12.5));
    CHECK(natural.width == doctest::Approx(25));
    label.width = SizeSpec::fixed(30);
    label.place({0, 0, 30, 40}, metrics);
    CHECK(label.lines().size() == 3);
    label.maxLines = 2;
    label.markDirty();
    label.place({0, 0, 30, 40}, metrics);
    CHECK(label.lines().size() == 2);
}

TEST_CASE("dirty marks propagate to the root and clear after layout") {
    FixedMetrics metrics;
    Box root(Axis::Column);
    Label* label = root.add<Label>("x");
    root.place({0, 0, 100, 100}, metrics);
    CHECK_FALSE(root.needsLayout());
    label->setText("changed");
    CHECK(root.needsLayout());
    root.place({0, 0, 100, 100}, metrics);
    CHECK_FALSE(root.needsLayout());
}

TEST_CASE("children can be taken out and the tree knows its ancestors") {
    Box root;
    Box* inner = root.add<Box>();
    View* leaf = inner->add<View>();
    CHECK(root.isAncestorOf(leaf));
    CHECK(root.containsPointer(leaf));
    auto taken = root.takeChild(inner);
    CHECK(taken->parent() == nullptr);
    CHECK_FALSE(root.containsPointer(leaf));
    CHECK(root.children().empty());
}
