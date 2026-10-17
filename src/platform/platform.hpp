#pragma once

#include <cstdint>
#include <string>

#include "platform/events.hpp"
#include "platform/video_layer.hpp"

namespace playfin::platform {

struct Config {
    int width = 1920;
    int height = 1080;
    bool hidden = false;
    std::string dataDirOverride;  // empty: the platform default
};

struct Size {
    int width = 0;
    int height = 0;
};

// Starts SDL, creates the data dir and opens playfin.log in it, then the window and an OpenGL ES 2 context. Logs the reason and returns false on
// failure.
bool init(const Config& config);
void shutdown();

// Next event (real input, then key repeat); false when there is none left for this frame.
bool pollEvent(Event& out, std::uint32_t nowMs);
void swap();
// Shows a frame drawn on the CPU (RGBA, top-down rows) over the whole window, then swaps. `changed` false shows
// the last frame again without uploading it.
// With a video layer: the video letterboxed first, then the UI frame over it (premultiplied alpha).
void presentFrame(const std::uint8_t* rgba, int width, int height, bool changed, const VideoLayer* video = nullptr);
Size drawableSize();
std::uint32_t ticksMs();
std::string systemLanguage();
// Minutes between the console's local time and UTC at `utcSeconds` (Paris in summer: 120).
int utcOffsetMinutes(std::int64_t utcSeconds);
std::string resourceDir();

}  // namespace playfin::platform
