#pragma once

#include <memory>
#include <string>

#include "ui/images.hpp"
#include "ui/view.hpp"

namespace playfin::ui {

// Image from a URL, requested at the view's size once laid out, faded in; the placeholder color shows meanwhile,
// shimmering while the picture is on its way, plain on failure.
class ImageView : public View {
public:
    enum class Fit { Cover, Contain };

    ImageView();
    void setUrl(std::string url);
    const std::string& url() const { return url_; }

    Fit fit = Fit::Cover;
    Color placeholder;
    // a screen's backdrop: the picture comes with the backdrop shade baked in (ImageShape::Backdrop)
    bool backdrop = false;
    // with `backdrop`: the current theme's treatment instead of the shade (ImageShape::ThemedBackdrop)
    bool themedBackdrop = false;
    // the picture is shown, opaque, over the whole frame (what is under it need not be drawn)
    bool coversFrame() const;

    void draw(NVGcontext* vg) const override;

protected:
    void layout(TextMetrics& metrics) override;

private:
    std::string url_;
    std::string requested_;
    std::unique_ptr<ImageRequest> request_;
    TextureRef texture_;
    float alpha_ = 0;
    bool failed_ = false;
    float requestedAtMs_ = 0;  // the shimmer stops after a while (a request that never answers)
};

}  // namespace playfin::ui
