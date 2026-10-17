#pragma once

#include <memory>
#include <string>

#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"
#include "ui/text.hpp"
#include "ui/view.hpp"

namespace playfin::app {

// Text styles and paddings shared by the screens.
ui::FontStyle bodyText();
ui::FontStyle bodyStrong();
ui::FontStyle headingText();
ui::FontStyle titleText(ui::FontWeight weight);
ui::Insets rowPadding();   // a one-line row
ui::Insets pagePadding();  // a scrolling page under the navigation bar

// First code point of a name, upper-cased for ASCII letters ("?" when empty).
std::string initialOf(const std::string& name);

// "Playfin." with the dot in the accent colour.
std::unique_ptr<ui::View> makeLogo();

// A 48 px circle in the accent colour with the user's initial.
std::unique_ptr<ui::View> makeAvatar(const std::string& userName);

// The background colour fading over a backdrop: opaque on the bottom 30 %, 56 % at the top (the backdrop shows
// as through 55 % opacity and a 20 % shade, while drawn opaque: the renderer copies it).
class BackdropShade : public ui::View {
public:
    BackdropShade();
    void draw(NVGcontext* vg) const override;
};

// The backdrop of an item (its own, else its series'), empty when it has none.
std::string backdropUrlFor(const jellyfin::Client& client, const jellyfin::Item& item, int width, int height);

}  // namespace playfin::app
