#include "app/shell/trickplay_preview.hpp"

#include <nanovg.h>

#include <algorithm>

#include "ui/context.hpp"
#include "ui/draw.hpp"
#include "ui/text.hpp"
#include "ui/theme.hpp"

namespace playfin::app {

static_assert(player::kMaxSheetSide == ui::kMaxImageSide && player::kMaxSheetPixels == ui::kMaxImagePixels,
              "the trickplay sheets chosen must stay within what the image decoder takes");

namespace {
constexpr float kWidth = 320;  // on screen, whatever the pictures' size; the height follows the video's shape
constexpr float kAbove = 16;    // between the picture's box and the bar
constexpr float kTimeHeight = 40;
}  // namespace

TrickplayPreview::TrickplayPreview() {
    name = "trickplay preview";
    width = ui::SizeSpec::fill();
    height = ui::SizeSpec::fill();
    visible = false;
}

TrickplayPreview::~TrickplayPreview() = default;

void TrickplayPreview::setPictures(const jellyfin::TrickplayInfo& info, std::function<std::string(int)> sheetUrl) {
    info_ = info;
    sheetUrl_ = std::move(sheetUrl);
    sheets_.clear();
}

void TrickplayPreview::want(int sheet) {
    if (sheet < 0 || sheet >= player::trickplaySheets(*info_) || sheets_.count(sheet) != 0) {
        return;
    }
    Sheet& entry = sheets_[sheet];
    if (ui::ImageLoader* images = ui::context().images) {
        const int w = info_->width * info_->tileWidth;
        const int h = info_->height * info_->tileHeight;
        entry.request = images->request(sheetUrl_(sheet), w, h, [this, sheet](ui::TextureRef texture) {
            auto found = sheets_.find(sheet);
            if (found != sheets_.end()) {
                found->second.texture = std::move(texture);
                markDirty();
            }
        });
    }
}

void TrickplayPreview::update(bool seeking, double target, double duration, int direction, const std::string& timeText) {
    const bool show = seeking && info_.has_value() && sheetUrl_;
    if (!show) {
        if (visible) {
            visible = false;
            markDirty();
        }
        return;
    }
    frame_ = player::trickplayFrame(*info_, target);
    fraction_ = duration > 0 ? std::min(1.0, std::max(0.0, target / duration)) : 0.0;
    time_ = timeText;
    wanted_ = sheetUrl_(frame_.sheet);
    // the shown sheet and the next one ahead; the others go
    const int ahead = frame_.sheet + (direction < 0 ? -1 : 1);
    for (auto it = sheets_.begin(); it != sheets_.end();) {
        it = it->first == frame_.sheet || it->first == ahead ? std::next(it) : sheets_.erase(it);
    }
    want(frame_.sheet);
    want(ahead);
    visible = true;
    markDirty();
}

ui::Size TrickplayPreview::pictureSize() const {
    if (!info_ || info_->width <= 0) {
        return {kWidth, kWidth * 9 / 16};
    }
    return {kWidth, kWidth * static_cast<float>(info_->height) / static_cast<float>(info_->width)};
}

void TrickplayPreview::draw(NVGcontext* vg) const {
    if (!info_ || anchor_ == nullptr) {
        return;
    }
    const auto found = sheets_.find(frame_.sheet);
    if (found == sheets_.end() || !found->second.texture) {
        return;  // not here yet: nothing rather than a placeholder
    }
    const ui::Texture& texture = *found->second.texture;
    const ui::Rect bar = anchor_->absoluteFrame();
    const ui::Rect self = absoluteFrame();
    const float pad = 6;
    const float pictureHeight = pictureSize().height;
    const float boxW = kWidth + 2 * pad;
    const float boxH = pictureHeight + 2 * pad + kTimeHeight;
    float x = bar.x + bar.width * static_cast<float>(fraction_) - boxW / 2 - self.x;
    x = std::max(bar.x - self.x, std::min(x, bar.x + bar.width - boxW - self.x));
    const float top = above_ != nullptr ? above_->absoluteFrame().y : bar.y;
    const float y = top - kAbove - boxH - self.y;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, boxW, boxH, ui::theme().radius);
    nvgFillColor(vg, ui::nvgColor(ui::withAlpha(ui::theme().surface, 0xF0)));
    nvgFill(vg);
    // the sheet scaled so that the frame's rectangle fills the picture's place (the sheet may come smaller)
    const float sheetW = static_cast<float>(info_->width * info_->tileWidth);
    const float scaleToTexture = sheetW > 0 ? static_cast<float>(texture.width) / sheetW : 1.0f;
    const float k = kWidth / (static_cast<float>(frame_.width) * scaleToTexture);
    const float px = x + pad;
    const float py = y + pad;
    const NVGpaint paint = nvgImagePattern(vg, px - static_cast<float>(frame_.x) * scaleToTexture * k,
                                           py - static_cast<float>(frame_.y) * scaleToTexture * k,
                                           static_cast<float>(texture.width) * k, static_cast<float>(texture.height) * k,
                                           0, texture.id, 1);
    nvgBeginPath(vg);
    nvgRect(vg, px, py, kWidth, pictureHeight);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgFontFace(vg, ui::fontFace(ui::FontWeight::SemiBold));
    nvgFontSize(vg, ui::theme().fontBody);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::nvgColor(ui::theme().text));
    nvgText(vg, x + boxW / 2, py + pictureHeight + kTimeHeight / 2 + pad / 2, time_.c_str(), nullptr);
}

}  // namespace playfin::app
