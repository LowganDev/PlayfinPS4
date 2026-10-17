#include "app/shell/format.hpp"

#include <cstdio>
#include <vector>

#include "app/shell/guide.hpp"
#include "ui/i18n.hpp"

namespace playfin::app {

namespace {

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " · ";
        }
        out += part;
    }
    return out;
}

}  // namespace

std::string errorText(const core::Error& error) {
    switch (error.kind) {
    case core::ErrorKind::InvalidAddress:
        return ui::tr("shell.error.invalid");
    case core::ErrorKind::Network:
        return ui::tr("shell.error.unreachable");
    case core::ErrorKind::Timeout:
        return ui::tr("shell.error.timeout");
    case core::ErrorKind::Tls:
        return ui::tr("shell.error.tls");
    case core::ErrorKind::UnsupportedServer:
        return ui::tr("shell.error.too_old", {error.message});
    case core::ErrorKind::Unauthorized:
        return ui::tr("shell.signin.wrong");
    case core::ErrorKind::BadResponse:
        if (error.message == "not a Jellyfin server") {
            return ui::tr("shell.error.not_jellyfin");
        }
        return ui::tr("shell.error.generic", {error.message});
    default:
        return ui::tr("shell.error.generic", {error.message});
    }
}

std::string metaLine(const jellyfin::Item& item) {
    std::vector<std::string> parts;
    if (item.productionYear > 0) {
        parts.push_back(std::to_string(item.productionYear));
    }
    if (item.runTimeTicks > 0) {
        const long long minutes = (item.runTimeTicks + 300000000LL) / 600000000LL;
        parts.push_back(ui::tr("shell.details.minutes", {std::to_string(minutes)}));
    }
    parts.push_back(item.officialRating);
    if (item.communityRating > 0) {
        char rating[16];
        std::snprintf(rating, sizeof(rating), "\xE2\x98\x85 %.1f", item.communityRating);
        parts.emplace_back(rating);
    }
    return join(parts);
}

std::string cardTitle(const jellyfin::Item& item) {
    if (item.type == jellyfin::ItemType::Episode && !item.seriesName.empty()) {
        return item.seriesName;
    }
    return item.name;
}

std::string cardSubtitle(const jellyfin::Item& item, int utcOffsetMinutes) {
    if (item.type == jellyfin::ItemType::Episode) {
        return ui::tr("shell.details.season_episode",
                      {std::to_string(item.parentIndexNumber), std::to_string(item.indexNumber)}) +
               " " + item.name;
    }
    if (item.type == jellyfin::ItemType::MusicAlbum || item.type == jellyfin::ItemType::Audio) {
        return !item.albumArtist.empty() ? item.albumArtist : !item.artists.empty() ? item.artists.front() : "";
    }
    if (item.type == jellyfin::ItemType::Program && item.startUtc > 0) {
        return timeRange(item, utcOffsetMinutes);
    }
    if (item.type == jellyfin::ItemType::Playlist && item.childCount > 0) {
        return ui::translator().plural("music.track_count", item.childCount);
    }
    if (item.productionYear > 0) {
        return std::to_string(item.productionYear);
    }
    return {};
}

std::string imageUrlFor(const jellyfin::Client& client, const jellyfin::Item& item, bool wide, int width, int height) {
    using jellyfin::ImageType;
    const auto& images = item.images;
    if (wide) {
        if (item.type == jellyfin::ItemType::Episode && !images.primary.empty()) {
            return client.imageUrl(item.id, ImageType::Primary, images.primary, width, height);
        }
        if (!images.thumb.empty()) {
            return client.imageUrl(item.id, ImageType::Thumb, images.thumb, width, height);
        }
        if (!images.parentThumbItemId.empty() && !images.parentThumb.empty()) {
            return client.imageUrl(images.parentThumbItemId, ImageType::Thumb, images.parentThumb, width, height);
        }
        if (!images.backdrops.empty()) {
            return client.imageUrl(item.id, ImageType::Backdrop, images.backdrops.front(), width, height);
        }
        if (!images.parentBackdropItemId.empty() && !images.parentBackdrops.empty()) {
            return client.imageUrl(images.parentBackdropItemId, ImageType::Backdrop, images.parentBackdrops.front(),
                                   width, height);
        }
    } else if (item.type == jellyfin::ItemType::Episode && !item.seriesId.empty() && !images.seriesPrimary.empty()) {
        return client.imageUrl(item.seriesId, ImageType::Primary, images.seriesPrimary, width, height);
    }
    if (!images.primary.empty()) {
        return client.imageUrl(item.id, ImageType::Primary, images.primary, width, height);
    }
    if (item.type == jellyfin::ItemType::Program && !item.channelId.empty()) {  // no picture: the channel's logo
        return client.imageUrl(item.channelId, ImageType::Primary, "", width, height);
    }
    return {};
}

std::string clockText(double seconds) {
    const long total = seconds > 0 ? static_cast<long>(seconds) : 0;
    const long hours = total / 3600;
    const long minutes = (total / 60) % 60;
    const long rest = total % 60;
    char text[32];
    if (hours > 0) {
        std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", hours, minutes, rest);
    } else {
        std::snprintf(text, sizeof(text), "%ld:%02ld", minutes, rest);
    }
    return text;
}

}  // namespace playfin::app
