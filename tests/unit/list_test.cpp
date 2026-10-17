#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "support/fixed_metrics.hpp"
#include "ui/animation.hpp"
#include "ui/box.hpp"
#include "ui/context.hpp"
#include "ui/focus.hpp"
#include "ui/list.hpp"
#include "ui/scroll.hpp"
#include <nanovg.h>
#include "render/nvg_sw.hpp"
#include "render/raster.hpp"
#include <vector>

using namespace playfin::ui;

namespace {

struct Item : View {
    int boundIndex = -1;
    Item() {
        focusable = true;
        width = SizeSpec::fill();
        height = SizeSpec::fill();
    }
};

struct Numbers : DataSource {
    int size;
    int created = 0;
    explicit Numbers(int count) : size(count) {}
    int count() const override { return size; }
    std::unique_ptr<View> create() override {
        ++created;
        return std::make_unique<Item>();
    }
    void bind(View& view, int index) override { static_cast<Item&>(view).boundIndex = index; }
};

struct Fixture {
    playfin::test::FixedMetrics metrics;
    FocusManager focus;
    Animator animator;
    Box root{Axis::Column};
    Fixture() {
        context().focus = &focus;
        context().animator = &animator;
        focus.setScope(&root);
    }
    ~Fixture() {
        root.clearChildren();
        context().focus = nullptr;
        context().animator = nullptr;
    }
    void settle() {
        for (int i = 0; i < 60; ++i) {
            animator.tick(1000.0f / 60.0f);
            root.place({0, 0, 1000, 700}, metrics);
        }
    }
    int focusedIndex() const { return static_cast<Item*>(focus.focused())->boundIndex; }
};

HList* addRow(Box& root, int count, Numbers** source = nullptr) {
    HList* row = root.add<HList>(Size{100, 150}, 10.0f);
    auto numbers = std::make_unique<Numbers>(count);
    if (source != nullptr) {
        *source = numbers.get();
    }
    row->setSource(std::move(numbers));
    return row;
}

}  // namespace

TEST_CASE("a long row only materializes a window of items") {
    Fixture f;
    Numbers* source = nullptr;
    HList* row = addRow(f.root, 1000, &source);
    f.settle();
    CHECK(row->materializedCount() <= 25);
    CHECK(row->viewAt(0) != nullptr);
    CHECK(row->viewAt(500) == nullptr);
    REQUIRE(f.focus.focus(row));
    CHECK(f.focusedIndex() == 0);
    for (int i = 0; i < 20; ++i) {
        CHECK(f.focus.move(Direction::Right));
        f.settle();
    }
    CHECK(row->focusedIndex() == 20);
    CHECK(f.focusedIndex() == 20);
    CHECK(row->scrollOffset() == doctest::Approx(20 * 110));
    CHECK(row->materializedCount() <= 25);
    CHECK(source->created <= 30);
}

TEST_CASE("the row stops at both ends and scrolls no further than its content") {
    Fixture f;
    HList* row = addRow(f.root, 4);
    f.settle();
    f.focus.focus(row);
    CHECK_FALSE(f.focus.move(Direction::Left));
    for (int i = 0; i < 3; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    CHECK_FALSE(f.focus.move(Direction::Right));
    CHECK(row->scrollOffset() == doctest::Approx(0));  // 4 items fit in 1000
}

TEST_CASE("near-end fires once per length when focus approaches the end") {
    Fixture f;
    Numbers* source = nullptr;
    HList* row = addRow(f.root, 10, &source);
    int calls = 0;
    row->onNearEnd(3, [&] { ++calls; });
    f.settle();
    f.focus.focus(row);
    for (int i = 0; i < 9; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    CHECK(calls == 1);
    source->size = 20;
    row->reload();
    f.settle();
    for (int i = 0; i < 10; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    CHECK(calls == 2);
}

TEST_CASE("shrinking the data clamps the focus onto a live item") {
    Fixture f;
    Numbers* source = nullptr;
    HList* row = addRow(f.root, 1000, &source);
    f.settle();
    f.focus.focus(row);
    for (int i = 0; i < 30; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    source->size = 10;
    row->reload();
    f.settle();
    CHECK(row->focusedIndex() == 9);
    REQUIRE(f.focus.focused() != nullptr);
    CHECK(f.focus.focused() == row->viewAt(9));
}

TEST_CASE("rows remember their item when focus comes back") {
    Fixture f;
    HList* first = addRow(f.root, 50);
    HList* second = addRow(f.root, 50);
    f.settle();
    f.focus.focus(first);
    for (int i = 0; i < 5; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    CHECK(f.focus.move(Direction::Down));
    f.settle();
    CHECK(second->focusedIndex() == 0);
    CHECK(f.focus.move(Direction::Up));
    CHECK(first->focusedIndex() == 5);
    CHECK(f.focusedIndex() == 5);
}

TEST_CASE("a grid moves by columns and rows") {
    Fixture f;
    Grid* grid = f.root.add<Grid>(Size{100, 150}, 10.0f, 5);
    grid->height = SizeSpec::fixed(500);
    grid->setSource(std::make_unique<Numbers>(98));
    f.settle();
    f.focus.focus(grid);
    f.focus.move(Direction::Right);
    f.focus.move(Direction::Right);
    CHECK(grid->focusedIndex() == 2);
    f.focus.move(Direction::Down);
    CHECK(grid->focusedIndex() == 7);
    f.focus.move(Direction::Right);
    f.focus.move(Direction::Right);
    CHECK(grid->focusedIndex() == 9);
    CHECK_FALSE(f.focus.move(Direction::Right));
    for (int i = 0; i < 30; ++i) {
        f.focus.move(Direction::Down);
        f.settle();
    }
    CHECK(grid->focusedIndex() == 97);  // last row: 95..97
    CHECK(grid->materializedCount() <= 60);
    CHECK(grid->scrollOffset() > 0);
}

TEST_CASE("a vertical scroll brings the focused row to the top") {
    Fixture f;
    VScroll* scroll = f.root.add<VScroll>();
    scroll->height = SizeSpec::fixed(500);
    scroll->width = SizeSpec::fill();
    auto column = std::make_unique<Box>(Axis::Column);
    column->gap = 20;
    std::vector<HList*> rows;
    for (int i = 0; i < 8; ++i) {
        rows.push_back(addRow(*column, 20));
    }
    scroll->setContent(std::move(column));
    f.settle();
    f.focus.focus(rows[0]);
    for (int i = 0; i < 3; ++i) {
        f.focus.move(Direction::Down);
        f.settle();
    }
    CHECK(rows[3]->focusedIndex() == 0);
    CHECK(scroll->scrollOffset() == doctest::Approx(rows[3]->frame.y));
    for (int i = 0; i < 3; ++i) {
        f.focus.move(Direction::Up);
        f.settle();
    }
    CHECK(scroll->scrollOffset() == doctest::Approx(0));
}

TEST_CASE("destroying a list while one of its items is focused clears the focus") {
    Fixture f;
    HList* row = addRow(f.root, 100);
    f.settle();
    f.focus.focus(row);
    f.focus.move(Direction::Right);
    f.root.clearChildren();
    CHECK(f.focus.focused() == nullptr);
}

TEST_CASE("the first focusable row keeps the content above it visible") {
    Fixture f;
    VScroll* scroll = f.root.add<VScroll>();
    scroll->height = SizeSpec::fixed(500);
    scroll->width = SizeSpec::fill();
    auto column = std::make_unique<Box>(Axis::Column);
    column->gap = 20;
    View* header = column->add<View>();
    header->height = SizeSpec::fixed(120);
    header->width = SizeSpec::fill();
    std::vector<HList*> rows;
    for (int i = 0; i < 6; ++i) {
        rows.push_back(addRow(*column, 20));
    }
    scroll->setContent(std::move(column));
    f.settle();
    f.focus.focus(rows[0]);
    f.settle();
    CHECK(scroll->scrollOffset() == doctest::Approx(0));
    f.focus.move(Direction::Down);
    f.settle();
    CHECK(scroll->scrollOffset() == doctest::Approx(rows[1]->frame.y));
}

TEST_CASE("a focusable header (the navigation bar) does not count as the first row") {
    Fixture f;
    VScroll* scroll = f.root.add<VScroll>();
    scroll->height = SizeSpec::fixed(500);
    scroll->width = SizeSpec::fill();
    auto column = std::make_unique<Box>(Axis::Column);
    column->gap = 20;
    HList* bar = addRow(*column, 20);
    std::vector<HList*> rows;
    for (int i = 0; i < 6; ++i) {
        rows.push_back(addRow(*column, 20));
    }
    scroll->setContent(std::move(column));
    scroll->setHeader(bar);
    f.settle();
    f.focus.focus(rows[0]);
    f.settle();
    CHECK(scroll->scrollOffset() == doctest::Approx(0));
    f.focus.focus(bar);
    f.settle();
    CHECK(scroll->scrollOffset() == doctest::Approx(0));
    f.focus.focus(rows[1]);
    f.settle();
    CHECK(scroll->scrollOffset() == doctest::Approx(rows[1]->frame.y));
}

namespace {

struct Other : View {
    Other() {
        focusable = true;
        width = SizeSpec::fill();
        height = SizeSpec::fill();
    }
};

struct Others : DataSource {
    int binds = 0;
    int wrongViews = 0;
    int count() const override { return 3; }
    std::unique_ptr<View> create() override { return std::make_unique<Other>(); }
    void bind(View& view, int) override {
        ++binds;
        if (dynamic_cast<Other*>(&view) == nullptr) {
            ++wrongViews;
        }
    }
};

}  // namespace

TEST_CASE("a new data source gets its own items and the focus moves to its first one") {
    Fixture f;
    HList* row = addRow(f.root, 50);
    f.settle();
    f.focus.focus(row);
    for (int i = 0; i < 5; ++i) {
        f.focus.move(Direction::Right);
        f.settle();
    }
    auto others = std::make_unique<Others>();
    Others* source = others.get();
    row->setSource(std::move(others));
    f.settle();
    CHECK(source->binds > 0);
    CHECK(source->wrongViews == 0);
    REQUIRE(f.focus.focused() != nullptr);
    CHECK(f.focus.focused() == row->viewAt(0));
}

namespace {
std::vector<int> gDrawn;
struct Recorded : View {
    int boundIndex = -1;
    Recorded() {
        focusable = true;
        width = SizeSpec::fill();
        height = SizeSpec::fill();
    }
    void draw(NVGcontext* vg) const override {
        gDrawn.push_back(boundIndex);
        View::draw(vg);
    }
};
struct RecordedSource : DataSource {
    int count() const override { return 5; }
    std::unique_ptr<View> create() override { return std::make_unique<Recorded>(); }
    void bind(View& view, int index) override { static_cast<Recorded&>(view).boundIndex = index; }
};
}  // namespace

TEST_CASE("the focused item is drawn after its neighbours (it grows over them)") {
    Fixture f;
    HList* row = f.root.add<HList>(Size{100, 150}, 10.0f);
    row->width = SizeSpec::fill();
    row->setSource(std::make_unique<RecordedSource>());
    f.settle();
    REQUIRE(f.focus.focus(row));
    REQUIRE(f.focus.move(Direction::Right));
    f.settle();
    playfin::render::Surface surface(64, 64);
    NVGcontext* vg = playfin::render::createSoftwareCanvas(surface, 1);
    gDrawn.clear();
    nvgBeginFrame(vg, 64, 64, 1);
    f.root.draw(vg);
    nvgCancelFrame(vg);
    playfin::render::destroySoftwareCanvas(vg);
    REQUIRE(gDrawn.size() >= 3);
    CHECK(gDrawn.back() == 1);
}

TEST_CASE("a scroll with a maximum height fits short content and caps long content") {
    Fixture f;
    auto* column = f.root.add<Box>(Axis::Column);
    VScroll* shortScroll = column->add<VScroll>();
    shortScroll->height = SizeSpec::fit();
    shortScroll->maxHeight = 300;
    auto shortContent = std::make_unique<View>();
    shortContent->height = SizeSpec::fixed(120);
    shortScroll->setContent(std::move(shortContent));
    VScroll* longScroll = column->add<VScroll>();
    longScroll->height = SizeSpec::fit();
    longScroll->maxHeight = 300;
    auto longContent = std::make_unique<View>();
    longContent->height = SizeSpec::fixed(900);
    longScroll->setContent(std::move(longContent));
    f.settle();
    CHECK(shortScroll->frame.height == doctest::Approx(120));
    CHECK(longScroll->frame.height == doctest::Approx(300));
}
