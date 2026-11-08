#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jellyfin/models.hpp"

namespace playfin::app {

// Programme guide helpers. Times are UTC seconds; `offsetMinutes` is the console's distance to UTC.

// "21:30" in local time
std::string clock24(std::int64_t utc, int offsetMinutes);
// "21:30 – 22:15"
std::string timeRange(const jellyfin::Item& program, int offsetMinutes);
// "Aujourd'hui", "Demain", then "Dimanche 18" (local days, compared with `now`)
std::string dayHeading(std::int64_t utc, std::int64_t now, int offsetMinutes);
// "Vendredi 16 · 21:45": the header's date and time
std::string headerClock(std::int64_t now, int offsetMinutes);
// local days since 1970 (for grouping)
std::int64_t localDay(std::int64_t utc, int offsetMinutes);

// The channels shown: all, the favourites, the recently watched (in that order), those whose programme on now is a
// film, sport, news, for children, or the radios; then those whose name or number holds `search` (any case).
enum class ChannelFilter { All, Favorites, Recent, Movies, Sports, News, Kids, Radio };
std::vector<jellyfin::Item> filterChannels(const std::vector<jellyfin::Item>& channels, ChannelFilter filter,
                                           const std::vector<std::string>& recentIds, const std::string& search);
// The filters worth offering for these channels (All always; the others when a channel would show).
std::vector<ChannelFilter> channelFilters(const std::vector<jellyfin::Item>& channels,
                                          const std::vector<std::string>& recentIds);
// `id` first in `recent`, at most 8
void addRecentChannel(std::vector<std::string>& recent, const std::string& id);

bool airing(const jellyfin::Item& program, std::int64_t now);
// 0 before the start, 1 after the end
double progress(const jellyfin::Item& program, std::int64_t now);
const jellyfin::Item* onNow(const std::vector<jellyfin::Item>& programs, std::int64_t now);

}  // namespace playfin::app
