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
// local days since 1970 (for grouping)
std::int64_t localDay(std::int64_t utc, int offsetMinutes);

bool airing(const jellyfin::Item& program, std::int64_t now);
// 0 before the start, 1 after the end
double progress(const jellyfin::Item& program, std::int64_t now);
const jellyfin::Item* onNow(const std::vector<jellyfin::Item>& programs, std::int64_t now);

}  // namespace playfin::app
