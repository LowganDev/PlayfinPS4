#include "player/tracks.hpp"

namespace playfin::player {

namespace {

std::string labelOf(const jellyfin::MediaStream& stream) {
    if (!stream.displayTitle.empty()) {
        return stream.displayTitle;
    }
    if (!stream.title.empty()) {
        return stream.title;
    }
    if (!stream.language.empty()) {
        return stream.language + " (" + stream.codec + ")";
    }
    return stream.codec;
}

std::vector<TrackItem> tracksOf(const jellyfin::MediaSource& source, jellyfin::StreamType type, int currentIndex) {
    int embedded = 0;
    for (const jellyfin::MediaStream& stream : source.streams) {
        embedded += stream.type == type && !stream.isExternal ? 1 : 0;
    }
    std::vector<TrackItem> items;
    int nextEmbedded = 1;
    int nextExternal = embedded + 1;
    for (const jellyfin::MediaStream& stream : source.streams) {
        if (stream.type != type) {
            continue;
        }
        TrackItem item;
        item.jellyfinIndex = stream.index;
        item.external = stream.isExternal;
        item.mpvId = stream.isExternal ? nextExternal++ : nextEmbedded++;
        item.label = labelOf(stream);
        item.current = stream.index == currentIndex;
        items.push_back(item);
    }
    return items;
}

}  // namespace

std::vector<TrackItem> audioTracks(const jellyfin::MediaSource& source, int currentIndex) {
    return tracksOf(source, jellyfin::StreamType::Audio, currentIndex);
}

std::vector<TrackItem> subtitleTracks(const jellyfin::MediaSource& source, int currentIndex,
                                      const std::string& noneLabel) {
    TrackItem none;
    none.label = noneLabel;
    none.current = currentIndex < 0;
    std::vector<TrackItem> items{none};
    for (TrackItem& item : tracksOf(source, jellyfin::StreamType::Subtitle, currentIndex)) {
        items.push_back(std::move(item));
    }
    return items;
}

bool needsRestart(bool transcoding, const jellyfin::MediaStream& stream) {
    if (stream.type == jellyfin::StreamType::Audio) {
        return transcoding;
    }
    return stream.type == jellyfin::StreamType::Subtitle && stream.deliveryMethod == "Encode";
}

}  // namespace playfin::player
