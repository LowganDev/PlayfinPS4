#pragma once

#include <string>
#include <vector>

#include "app/shell/session.hpp"
#include "jellyfin/models.hpp"
#include "ui/image_view.hpp"
#include "ui/box.hpp"
#include "ui/view.hpp"

namespace playfin::app {

// The backdrop of the focused item behind a screen, for the themes whose background follows the focus
// (Ultrachromic, Abyss, Zesty, Finimalism): the picture comes with the theme's treatment baked in. It changes 400 ms
// after the focus settles (a quick scroll loads nothing), keeps the last picture when the focus goes to something
// without one, and stays hidden, asking for nothing, in the other themes. First among the screen's children. Two
// layers: a new picture loads and fades in over the last one, which stays under it until then (no dark flash).
class ThemeBackdrop : public ui::Stack {
public:
    static constexpr float kDelayMs = 400;

    explicit ThemeBackdrop(AppSession& session);
    ~ThemeBackdrop() override;
    void follow(const jellyfin::Item& item);
    // A view and its ancestors: the item of the card among them, if any.
    void followFocus(ui::View* focused);
    const std::string& shownUrl() const { return shown_; }
    // the layers' pictures, bottom first
    std::vector<std::string> layerUrls() const;
    void onThemeChanged() override;

private:
    ui::ImageView* addLayer();
    AppSession& session_;
    std::string shown_;
    std::string pending_;
};

}  // namespace playfin::app
