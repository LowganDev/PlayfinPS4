#include "ui/image_view.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>

#include "ui/animation.hpp"
#include "ui/context.hpp"
#include "ui/draw.hpp"

namespace playfin::ui {

ImageView::ImageView() : placeholder(theme().surface) { cornerRadius = theme().radius; }

void ImageView::setUrl(std::string url) {
    if (url == url_) {
        return;
    }
    url_ = std::move(url);
    requested_.clear();
    request_.reset();
    texture_.reset();
    alpha_ = 0;
    failed_ = false;
    markDirty();
}

void ImageView::layout(TextMetrics& metrics) {
    (void)metrics;
    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);
    const ImageShape shape = backdrop ? (themedBackdrop ? ImageShape::ThemedBackdrop : ImageShape::Backdrop)
                             : fit == Fit::Cover ? ImageShape::Crop
                                                 : ImageShape::Whole;
    const std::string key = url_ + "|" + std::to_string(w) + "x" + std::to_string(h) + "/" +
                            std::to_string(static_cast<int>(shape)) + (themedBackdrop ? "/" + theme().id : std::string());
    if (url_.empty() || w <= 0 || h <= 0 || key == requested_ || context().images == nullptr) {
        return;
    }
    requested_ = key;
    requestedAtMs_ = context().timeMs;
    request_ = context().images->request(url_, w, h, [this](TextureRef texture) {
        texture_ = std::move(texture);
        failed_ = !texture_;
        if (texture_ && fadeIn && context().animator != nullptr) {
            alpha_ = 0;
            context().animator->animate(&alpha_, 1, theme().fadeMs, Easing::Linear, this);
        } else {
            alpha_ = 1;
        }
    }, shape);
}

bool ImageView::coversFrame() const { return texture_ && texture_->opaque && fit == Fit::Cover && alpha_ >= 1; }

void ImageView::draw(NVGcontext* vg) const {
    // an opaque picture covering the frame hides the placeholder entirely: not drawing it saves a full fill
    const bool covered = coversFrame();
    if (!covered) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, frame.width, frame.height, cornerRadius);
        Color fill = placeholder;
        constexpr float kShimmerMs = 20000;
        if (!texture_ && !failed_ && request_ && placeholder.a == 255 &&
            context().timeMs - requestedAtMs_ < kShimmerMs) {
            // on its way: the placeholder breathes towards the raised surface (1.2 s), cheap: one plain fill
            const double phase = std::fmod(static_cast<double>(context().timeMs), 1200.0) / 1200.0;
            const float t = static_cast<float>(0.5 - 0.5 * std::cos(phase * 6.283185307179586)) * 0.6f;
            const Color to = theme().surfaceRaised;
            fill = Color{static_cast<std::uint8_t>(fill.r + (to.r - fill.r) * t),
                         static_cast<std::uint8_t>(fill.g + (to.g - fill.g) * t),
                         static_cast<std::uint8_t>(fill.b + (to.b - fill.b) * t), 255};
            context().redrawNextFrame = true;
        }
        nvgFillColor(vg, nvgColor(fill));
        nvgFill(vg);
    }
    if (texture_ && alpha_ > 0) {
        const float tw = static_cast<float>(texture_->width);
        const float th = static_cast<float>(texture_->height);
        // a picture cut to the frame's whole-pixel size is shown 1:1, on whole pixels (the CPU renderer copies its
        // rows; the shift is under half a pixel)
        const bool exact = fit == Fit::Cover && texture_->width == static_cast<int>(frame.width) &&
                           texture_->height == static_cast<int>(frame.height);
        float shift[2] = {0, 0};
        float xform[6];
        nvgCurrentTransform(vg, xform);
        if (exact && xform[0] == 1 && xform[1] == 0 && xform[2] == 0 && xform[3] == 1) {
            shift[0] = static_cast<float>(std::round(static_cast<double>(xform[4]))) - xform[4];
            shift[1] = static_cast<float>(std::round(static_cast<double>(xform[5]))) - xform[5];
        }
        const float scale = exact ? 1.0f
                            : fit == Fit::Cover ? std::max(frame.width / tw, frame.height / th)
                                                : std::min(frame.width / tw, frame.height / th);
        const float pw = tw * scale;
        const float ph = th * scale;
        const float ox = exact ? 0.0f : (frame.width - pw) / 2;
        const float oy = exact ? 0.0f : (frame.height - ph) / 2;
        nvgSave(vg);
        nvgTranslate(vg, shift[0], shift[1]);
        const NVGpaint paint = nvgImagePattern(vg, ox, oy, pw, ph, 0, texture_->id, alpha_);
        nvgBeginPath(vg);
        if (fit == Fit::Cover) {
            nvgRoundedRect(vg, 0, 0, frame.width, frame.height, cornerRadius);
        } else {
            nvgRoundedRect(vg, ox, oy, pw, ph, cornerRadius);
        }
        nvgFillPaint(vg, paint);
        nvgFill(vg);
        nvgRestore(vg);
    }
    drawChildren(vg);
}

}  // namespace playfin::ui
