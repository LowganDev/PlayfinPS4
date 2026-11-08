#include "app/shell/format.hpp"

#include <algorithm>
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
    if (item.type == jellyfin::ItemType::Person) {
        return item.role;
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

std::vector<std::string> qualityChips(const jellyfin::MediaSource& source) {
    std::vector<std::string> chips;
    const jellyfin::MediaStream* video = nullptr;
    const jellyfin::MediaStream* audio = nullptr;
    for (const jellyfin::MediaStream& stream : source.streams) {
        if (stream.type == jellyfin::StreamType::Video && video == nullptr) {
            video = &stream;
        } else if (stream.type == jellyfin::StreamType::Audio && (audio == nullptr || stream.isDefault)) {
            audio = audio != nullptr && audio->isDefault ? audio : &stream;
        }
    }
    if (video != nullptr) {
        const int w = video->width;
        const int h = video->height;
        if (w >= 3200 || h >= 2000) {
            chips.emplace_back("4K");
        } else if (w >= 1800 || h >= 1000) {
            chips.emplace_back("1080p");
        } else if (w >= 1200 || h >= 700) {
            chips.emplace_back("720p");
        } else if (w > 0 || h > 0) {
            chips.emplace_back("SD");
        }
        const std::string& range = video->videoRangeType;
        if (range.rfind("DOVI", 0) == 0) {
            chips.emplace_back("Dolby Vision");
        } else if (range == "HDR10Plus") {
            chips.emplace_back("HDR10+");
        } else if (!range.empty() && range != "SDR" && range != "Unknown") {
            chips.push_back(range);
        }
        static const std::pair<const char*, const char*> kCodecs[] = {
            {"hevc", "HEVC"}, {"h265", "HEVC"}, {"av1", "AV1"}, {"vp9", "VP9"}, {"h264", "H.264"}};
        for (const auto& codec : kCodecs) {
            if (video->codec == codec.first) {
                chips.emplace_back(codec.second);
                break;
            }
        }
    }
    if (audio != nullptr && audio->channels > 0) {
        chips.push_back(audio->channels >= 8   ? "7.1"
                        : audio->channels >= 6 ? "5.1"
                        : audio->channels == 1 ? ui::tr("shell.details.mono")
                                               : ui::tr("shell.details.stereo"));
    }
    return chips;
}

std::string directorsLine(const jellyfin::Item& item) {
    std::string names;
    for (const jellyfin::Person& person : item.people) {
        if (person.type == "Director") {
            names += (names.empty() ? "" : ", ") + person.name;
        }
    }
    return names.empty() ? std::string() : ui::tr("shell.details.directed_by", {names});
}

std::string endsAtText(const jellyfin::Item& item, std::int64_t now, int utcOffsetMinutes) {
    if (item.runTimeTicks <= 0) {
        return {};
    }
    const std::int64_t left = (item.runTimeTicks - std::max<std::int64_t>(0, item.userData.positionTicks)) / 10000000;
    const std::int64_t end = now + std::max<std::int64_t>(0, left);
    return ui::tr("player.ends_at", {clock24(end, utcOffsetMinutes)});
}

}  // namespace playfin::app
