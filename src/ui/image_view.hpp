#pragma once

#include <memory>
#include <string>

#include "ui/images.hpp"
#include "ui/view.hpp"

namespace playfin::ui {

// Image from a URL, requested at the view's size once laid out, faded in; the placeholder color shows meanwhile
// (and on failure).
class ImageView : public View {
public:
    enum class Fit { Cover, Contain };

    ImageView();
    void setUrl(std::string url);
    const std::string& url() const { return url_; }

    Fit fit = Fit::Cover;
    Color placeholder;

    void draw(NVGcontext* vg) const override;

protected:
    void layout(TextMetrics& metrics) override;

private:
    std::string url_;
    std::string requested_;
    std::unique_ptr<ImageRequest> request_;
    TextureRef texture_;
    float alpha_ = 0;
};

}  // namespace playfin::ui
