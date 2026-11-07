#pragma once

#include <cstdint>

namespace playfin::platform {

// How the picture takes the screen: whole with black bars, filling it (the overflow cut), or stretched to it.
enum class VideoFit { Fit, Fill, Stretch };

// A video picture shown under the UI: rgb0 bytes (R, G, B, unused), top-down rows of 4 × width bytes. The
// presenter places it on the screen (render::videoBox) and scales it on the GPU.
struct VideoLayer {
    const std::uint8_t* rgb0 = nullptr;
    int width = 0;
    int height = 0;
    bool changed = false;  // a new picture since the last frame: upload it
    VideoFit fit = VideoFit::Fit;
    double scale = 1.0;  // of the screen the picture may take (0.8 to 1: TVs that cut the edges)
};

}  // namespace playfin::platform
