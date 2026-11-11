#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "jellyfin/models.hpp"
#include "player/trickplay.hpp"
#include "ui/images.hpp"
#include "ui/view.hpp"

namespace playfin::app {

// While seeking, the server's picture of the target time above the player's bar, over the target, with the time under
// it. Sheets come through the image loader; the next one in the seek's direction is fetched ahead. With no
// pictures for the video, or none arrived yet, nothing shows: no placeholder, no message.
class TrickplayPreview : public ui::View {
public:
    TrickplayPreview();
    ~TrickplayPreview() override;
    // The pictures of the playing version and where its sheets are; `sheetUrl(sheet)` gives a sheet's address.
    void setPictures(const jellyfin::TrickplayInfo& info, std::function<std::string(int sheet)> sheetUrl);
    // `track`: where along the progress bar the target is; `above`: what the preview sits above (the player's bar)
    void setAnchor(const ui::View* track, const ui::View* above) {
        anchor_ = track;
        above_ = above;
    }
    // Shown while `seeking`, at `target` of `duration` seconds; `direction` (-1, 1) picks the sheet fetched ahead.
    void update(bool seeking, double target, double duration, int direction, const std::string& timeText);
    const std::string& wantedUrl() const { return wanted_; }
    // The picture's size on screen: 320 wide, as tall as the video's shape makes it.
    ui::Size pictureSize() const;
    void draw(NVGcontext* vg) const override;

private:
    void want(int sheet);
    std::optional<jellyfin::TrickplayInfo> info_;
    std::function<std::string(int)> sheetUrl_;
    const ui::View* anchor_ = nullptr;
    const ui::View* above_ = nullptr;
    player::TrickplayFrame frame_;
    double fraction_ = 0;
    std::string time_;
    std::string wanted_;
    struct Sheet {
        std::unique_ptr<ui::ImageRequest> request;
        ui::TextureRef texture;
    };
    std::map<int, Sheet> sheets_;  // the shown sheet and the one fetched ahead
};

}  // namespace playfin::app
