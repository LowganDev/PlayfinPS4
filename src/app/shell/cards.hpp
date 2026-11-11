#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/shell/session.hpp"
#include "jellyfin/models.hpp"
#include "ui/box.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"
#include "ui/list.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

// Poster (323x485), wide (468x264) or square (323x323, music) card: image with its resume bar, title, subtitle.
// The page is 1728 px wide (1920 less 96 on each side): wide cards show 3.5 to a row (the next one peeks at the
// edge), posters and squares fill a grid of kGridColumns.
constexpr int kGridColumns = 5;
enum class CardShape { Wide, Poster, Square };
inline CardShape shapeOf(bool wide) { return wide ? CardShape::Wide : CardShape::Poster; }
ui::Size cardImageSize(CardShape shape);
ui::Size cardSize(CardShape shape);
inline ui::Size cardImageSize(bool wide) { return cardImageSize(shapeOf(wide)); }
inline ui::Size cardSize(bool wide) { return cardSize(shapeOf(wide)); }

// Focused, the card steps into the light: it grows to 1.07 and rises 6 px around its image's centre, and stands
// out as the theme says (FocusStyle): the accent's glow under it and an outline (Glow), a ring (Ring, with a light
// band across it when focusSheen), or a white frame on its edge (Frame). A theme's cardBorder outlines it always.
class ItemCard : public ui::Box {
public:
    explicit ItemCard(CardShape shape);
    explicit ItemCard(bool wide) : ItemCard(shapeOf(wide)) {}
    ~ItemCard() override;
    void show(const jellyfin::Item& item, const std::string& imageUrl, int utcOffsetMinutes);
    const jellyfin::Item& item() const { return item_; }  // the one shown (empty before show())
    std::function<void()> onPress;
    float focusProgress() const { return focus_; }
    void onFocusChanged(bool focused) override;
    void draw(NVGcontext* vg) const override;

private:
    void drawBorder(NVGcontext* vg, const ui::Rect& picture) const;
    float focus_ = 0;
    jellyfin::Item item_;
    ui::ImageView* image_;
    ui::ProgressBar* progress_;
    ui::Label* title_;
    ui::Label* subtitle_;
};

// A user on the profiles screen: a 180 px circle with the user's picture (else the initial on the accent) and the
// name under it; focused, it grows and takes an outline of the accent.
class ProfileCard : public ui::Box {
public:
    ProfileCard(const std::string& name, const std::string& imageUrl, std::string symbol, std::function<void()> onPress);
    ~ProfileCard() override;
    void onFocusChanged(bool focused) override;
    void onThemeChanged() override;
    void draw(NVGcontext* vg) const override;

private:
    float focus_ = 0;
    ui::Stack* circle_;
    ui::Label* initial_;
};

class ItemsSource : public ui::DataSource {
public:
    ItemsSource(AppSession& session, std::vector<jellyfin::Item> items, CardShape shape,
                std::function<void(const jellyfin::Item&)> onPress);
    ItemsSource(AppSession& session, std::vector<jellyfin::Item> items, bool wide,
                std::function<void(const jellyfin::Item&)> onPress)
        : ItemsSource(session, std::move(items), shapeOf(wide), std::move(onPress)) {}
    int count() const override { return static_cast<int>(items_.size()); }
    std::unique_ptr<ui::View> create() override;
    void bind(ui::View& view, int index) override;
    std::vector<jellyfin::Item>& items() { return items_; }

private:
    AppSession& session_;
    std::vector<jellyfin::Item> items_;
    CardShape shape_;
    std::function<void(const jellyfin::Item&)> onPress_;
};

// Section (title + horizontal row of cards) appended to `column`; returns the row.
ui::HList* addItemRow(ui::Box& column, const std::string& title, AppSession& session,
                      std::vector<jellyfin::Item> items, CardShape shape,
                      std::function<void(const jellyfin::Item&)> onPress);
inline ui::HList* addItemRow(ui::Box& column, const std::string& title, AppSession& session,
                             std::vector<jellyfin::Item> items, bool wide,
                             std::function<void(const jellyfin::Item&)> onPress) {
    return addItemRow(column, title, session, std::move(items), shapeOf(wide), std::move(onPress));
}

// Page layout shared by the shell screens: padded column with a heading.
ui::Box* addPage(ui::View& screen, const std::string& title);

}  // namespace playfin::app
