#pragma once

#include <cstdint>
#include <vector>

#include "platform/video_layer.hpp"

namespace playfin::render {

struct Box {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

// Where a video of the given size shows on the screen: as large as possible, aspect ratio kept, centred.
Box letterbox(int videoWidth, int videoHeight, int screenWidth, int screenHeight);
// Where it shows for a fit, inside the centred part of the screen `scale` gives (0.5 to 1). Fill may go past the
// screen's edges (the presenter's viewport cuts them).
Box videoBox(int videoWidth, int videoHeight, int screenWidth, int screenHeight, platform::VideoFit fit,
             double scale);

// The CPU equivalent of the presenter with a video layer (test screenshots only): black, the letterboxed video
// (nearest texel), then the premultiplied UI over it. `out` is RGBA, opaque.
void compositeForTest(std::vector<std::uint8_t>& out, int width, int height, const platform::VideoLayer& video,
                      const std::vector<std::uint8_t>& ui);

}  // namespace playfin::render
