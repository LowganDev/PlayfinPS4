#include "render/composite.hpp"

#include <algorithm>
#include <cmath>

namespace playfin::render {

Box letterbox(int videoWidth, int videoHeight, int screenWidth, int screenHeight) {
    if (videoWidth <= 0 || videoHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) {
        return {};
    }
    const double scale = std::min(static_cast<double>(screenWidth) / videoWidth,
                                  static_cast<double>(screenHeight) / videoHeight);
    Box box;
    box.width = static_cast<float>(videoWidth * scale);
    box.height = static_cast<float>(videoHeight * scale);
    box.x = (static_cast<float>(screenWidth) - box.width) / 2;
    box.y = (static_cast<float>(screenHeight) - box.height) / 2;
    return box;
}

void compositeForTest(std::vector<std::uint8_t>& out, int width, int height, const platform::VideoLayer& video,
                      const std::vector<std::uint8_t>& ui) {
    out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    const Box box = letterbox(video.width, video.height, width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* d = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x)) * 4];
            d[3] = 255;
            const double u = (x + 0.5 - box.x) / box.width;
            const double v = (y + 0.5 - box.y) / box.height;
            if (video.rgb0 != nullptr && box.width > 0 && u >= 0 && u < 1 && v >= 0 && v < 1) {
                const auto sx = static_cast<std::size_t>(std::floor(u * video.width));
                const auto sy = static_cast<std::size_t>(std::floor(v * video.height));
                const std::uint8_t* s = &video.rgb0[(sy * static_cast<std::size_t>(video.width) + sx) * 4];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
            }
            if (ui.size() >= out.size()) {
                const std::uint8_t* o = &ui[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(x)) * 4];
                const int keep = 255 - o[3];
                for (int k = 0; k < 3; ++k) {
                    d[k] = static_cast<std::uint8_t>(std::min(255, o[k] + (d[k] * keep + 127) / 255));
                }
            }
        }
    }
}

}  // namespace playfin::render
