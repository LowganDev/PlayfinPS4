#pragma once

#include <string>
#include <vector>

#include "jellyfin/models.hpp"

namespace playfin::player {

struct TrackItem {
    int jellyfinIndex = -1;  // -1: "no subtitles"
    int mpvId = 0;           // mpv's track id (1-based per type; external subtitles after the embedded ones)
    std::string label;
    bool external = false;
    bool current = false;
};

// The audio streams of a source, in order.
std::vector<TrackItem> audioTracks(const jellyfin::MediaSource& source, int currentIndex);
// "none" first, then the subtitle streams in order.
std::vector<TrackItem> subtitleTracks(const jellyfin::MediaSource& source, int currentIndex,
                                      const std::string& noneLabel);

// Whether picking this stream needs a new PlaybackInfo (the stream restarts at the current position): audio in a
// transcode, or a subtitle the server must burn in.
bool needsRestart(bool transcoding, const jellyfin::MediaStream& stream);

}  // namespace playfin::player
