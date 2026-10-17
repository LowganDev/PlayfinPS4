#pragma once

#include "platform/video_layer.hpp"

namespace playfin::ui {

// A screen that shows a video under the UI (the player). Registered with Application::setVideoSource; the frame
// loop then asks for a new picture every frame and draws the UI over it on a transparent surface.
class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual bool renderVideo() = 0;  // true: a new picture since the last call
    virtual platform::VideoLayer videoLayer() const = 0;
};

}  // namespace playfin::ui
