#pragma once

#include <cstdint>
#include <string>

namespace playfin::player {

// What the console decodes in real time (measured on the console): H.264, HEVC (8 and
// 10 bit), VP9, AV1, MPEG-2/4 up to 1920x1080; anything larger is transcoded.
struct DirectPlayLimits {
    int maxWidth = 1920;
    int maxHeight = 1080;
    std::int64_t maxBitrate = 40000000;  // the quality setting
};

// The DeviceProfile sent with PlaybackInfo: direct play of common video and audio containers, HLS H.264 transcodes
// (AAC over HTTP for music), subtitle delivery (mpv renders embedded text and bitmap subtitles), MaxStreamingBitrate
// from the limits.
std::string deviceProfileJson(const DirectPlayLimits& limits);

}  // namespace playfin::player
