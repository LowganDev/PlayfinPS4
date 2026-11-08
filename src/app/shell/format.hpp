#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"

namespace playfin::app {

std::string errorText(const core::Error& error);
// "1:02:03", "2:00" (whole seconds, negative as 0)
std::string clockText(double seconds);
// "2008 · 10 min · PG · ★ 6.5" (parts that are missing are skipped)
std::string metaLine(const jellyfin::Item& item);
// Episodes show their series; everything else its own name.
std::string cardTitle(const jellyfin::Item& item);
// Episodes: "S1 · E2 Name"; albums and tracks: the artist; playlists: the count; programmes: their times at
// `utcOffsetMinutes` (AppSession::utcOffsetMinutes); movies and series: the year.
std::string cardSubtitle(const jellyfin::Item& item, int utcOffsetMinutes = 0);
// Wide: episode image, thumb, series thumb, backdrop, series backdrop, primary. Poster: series primary for
// episodes, else primary. Empty when the item has no usable image.
// The quality of a version: resolution (4K, 1080p, 720p, SD), dynamic range (HDR10, Dolby Vision, HLG…), video
// codec, audio channels (Stereo, 5.1, 7.1).
std::vector<std::string> qualityChips(const jellyfin::MediaSource& source);
// "Réalisation : A, B" (empty without a director)
std::string directorsLine(const jellyfin::Item& item);
// "Se termine à 22:51": `now` plus what is left to watch, in local time; empty without a duration.
std::string endsAtText(const jellyfin::Item& item, std::int64_t now, int utcOffsetMinutes);
std::string imageUrlFor(const jellyfin::Client& client, const jellyfin::Item& item, bool wide, int width, int height);

}  // namespace playfin::app
