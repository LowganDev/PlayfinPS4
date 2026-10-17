#pragma once

#include <string>

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
// Episodes: "S1 · E2 Name"; albums and tracks: the artist; playlists: the count; movies and series: the year.
std::string cardSubtitle(const jellyfin::Item& item);
// Wide: episode image, thumb, series thumb, backdrop, series backdrop, primary. Poster: series primary for
// episodes, else primary. Empty when the item has no usable image.
std::string imageUrlFor(const jellyfin::Client& client, const jellyfin::Item& item, bool wide, int width, int height);

}  // namespace playfin::app
