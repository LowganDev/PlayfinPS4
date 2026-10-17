#include "app/shell/guide.hpp"

#include <cstdio>

#include "ui/i18n.hpp"

namespace playfin::app {

namespace {

std::int64_t local(std::int64_t utc, int offsetMinutes) { return utc + static_cast<std::int64_t>(offsetMinutes) * 60; }

std::int64_t floorDiv(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

}  // namespace

std::int64_t localDay(std::int64_t utc, int offsetMinutes) { return floorDiv(local(utc, offsetMinutes), 86400); }

std::string clock24(std::int64_t utc, int offsetMinutes) {
    const std::int64_t seconds = local(utc, offsetMinutes) - localDay(utc, offsetMinutes) * 86400;
    char text[8];
    std::snprintf(text, sizeof(text), "%02d:%02d", static_cast<int>(seconds / 3600),
                  static_cast<int>(seconds / 60 % 60));
    return text;
}

std::string timeRange(const jellyfin::Item& program, int offsetMinutes) {
    return clock24(program.startUtc, offsetMinutes) + " \xE2\x80\x93 " + clock24(program.endUtc, offsetMinutes);
}

std::string dayHeading(std::int64_t utc, std::int64_t now, int offsetMinutes) {
    const std::int64_t day = localDay(utc, offsetMinutes);
    const std::int64_t today = localDay(now, offsetMinutes);
    if (day == today) {
        return ui::tr("livetv.today");
    }
    if (day == today + 1) {
        return ui::tr("livetv.tomorrow");
    }
    // 1970-01-01 was a Thursday (4)
    const int weekday = static_cast<int>(((day % 7) + 7 + 4) % 7);
    // day of the month: from the civil date of the local day
    const std::string iso = jellyfin::utcText(day * 86400);
    const int dayOfMonth = std::stoi(iso.substr(8, 2));
    return ui::tr("livetv.date", {ui::tr("livetv.weekday." + std::to_string(weekday)), std::to_string(dayOfMonth)});
}

bool airing(const jellyfin::Item& program, std::int64_t now) { return program.startUtc <= now && now < program.endUtc; }

double progress(const jellyfin::Item& program, std::int64_t now) {
    if (program.endUtc <= program.startUtc || now <= program.startUtc) {
        return now >= program.endUtc && program.endUtc > 0 ? 1.0 : 0.0;
    }
    if (now >= program.endUtc) {
        return 1.0;
    }
    return static_cast<double>(now - program.startUtc) / static_cast<double>(program.endUtc - program.startUtc);
}

const jellyfin::Item* onNow(const std::vector<jellyfin::Item>& programs, std::int64_t now) {
    for (const auto& program : programs) {
        if (airing(program, now)) {
            return &program;
        }
    }
    return nullptr;
}

}  // namespace playfin::app
