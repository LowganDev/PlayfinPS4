#pragma once

#include <cstdint>

namespace playfin::platform {

// A video picture shown under the UI: rgb0 bytes (R, G, B, unused), top-down rows of 4 × width bytes. The
// presenter letterboxes it on the screen and scales it on the GPU.
struct VideoLayer {
    const std::uint8_t* rgb0 = nullptr;
    int width = 0;
    int height = 0;
    bool changed = false;  // a new picture since the last frame: upload it
};

}  // namespace playfin::platform
