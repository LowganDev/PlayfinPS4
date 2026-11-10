#include "player/session.hpp"

#include <cctype>
#include <cstring>

#include "core/log.hpp"
#include "net/url.hpp"
#include "player/stream_url.hpp"

namespace playfin::player {

namespace {

constexpr std::int64_t kProgressMs = 10000;
constexpr double kNextCardSeconds = 10;
constexpr std::int64_t kButtonMs = 8000;

// A path of the server joined to its address; an absolute URL only when it is on the same server (the
// Authorization header goes with every stream request). Empty otherwise.
std::string absolute(const jellyfin::Session& session, const std::string& path) {
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        const auto url = net::parseUrl(path.substr(0, path.find_first_of("?#")));
        const auto base = net::parseUrl(session.baseUrl);
        if (url && base && net::origin(*url) == net::origin(*base)) {
            return path;
        }
        return {};
    }
    return session.baseUrl + (path.empty() || path[0] == '/' ? "" : "/") + path;
}

}  // namespace

std::string directSourceUrl(const jellyfin::MediaSource& source, bool https) {
    if (!source.isInfiniteStream || source.protocol != "Http") {
        return {};
    }
    const std::string& path = source.path;
    if (path.rfind("http://", 0) != 0 && !(https && path.rfind("https://", 0) == 0)) {
        return {};
    }
    return path;
}

core::Result<StreamChoice> chooseStream(const jellyfin::Session& session, const std::string& itemId,
                                        const jellyfin::PlaybackInfo& info, MediaKind kind, jellyfin::PlayMethod method) {
    const bool forceDirect = method == jellyfin::PlayMethod::DirectOnly;
    const bool refused = !info.errorCode.empty() && !(forceDirect && info.errorCode == "NoCompatibleStream");
    if (refused || info.sources.empty()) {
        return core::makeError(core::ErrorKind::BadResponse,
                               info.errorCode.empty() ? "no media source" : info.errorCode);
    }
    const jellyfin::MediaSource& source = info.sources.front();
    StreamChoice choice;
    choice.mediaSourceId = source.id;
    choice.playSessionId = info.playSessionId;
    choice.liveStreamId = source.liveStreamId;
    choice.sourceUrl = directSourceUrl(source);
    if (source.supportsDirectPlay || forceDirect) {
        choice.url = directStreamUrl(session, itemId, source.id, info.playSessionId, kind);
    } else if (!source.transcodingUrl.empty() && !absolute(session, source.transcodingUrl).empty()) {
        choice.transcoding = true;
        choice.url = withoutApiKey(absolute(session, source.transcodingUrl));
    } else {
        return core::makeError(core::ErrorKind::BadResponse, "the server cannot stream this file");
    }
    for (const jellyfin::MediaStream& stream : source.streams) {
        if (stream.type == jellyfin::StreamType::Subtitle && stream.deliveryMethod == "External" &&
            !absolute(session, stream.deliveryUrl).empty()) {
            choice.externalSubtitles.push_back({stream.index, withoutApiKey(absolute(session, stream.deliveryUrl))});
        }
    }
    return choice;
}

void Reporter::onFirstFrame(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (started_) {
        return;
    }
    started_ = true;
    lastMs_ = nowMs;
    send_(ReportKind::Start, state);
}

void Reporter::tick(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (started_ && !stopped_ && nowMs - lastMs_ >= kProgressMs) {
        progressNow(nowMs, state);
    }
}

void Reporter::progressNow(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (!started_ || stopped_) {
        return;
    }
    lastMs_ = nowMs;
    send_(ReportKind::Progress, state);
}

void Reporter::stop(const jellyfin::PlaybackState& state) {
    if (!started_ || stopped_) {
        return;
    }
    stopped_ = true;
    send_(ReportKind::Stopped, state);
}

namespace {

// lower case, ASCII letters only (accents dropped: "Générique" -> "gnrique" would lose letters, so é/è/ê -> e)
std::string folded(const std::string& name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == 0xC3 && i + 1 < name.size()) {  // Latin-1 letters in UTF-8
            const unsigned char next = static_cast<unsigned char>(name[++i]);
            if ((next >= 0xA8 && next <= 0xAB) || (next >= 0x88 && next <= 0x8B)) {
                out += 'e';
            }
            continue;
        }
        if (std::isalpha(c)) {
            out += static_cast<char>(std::tolower(c));
        } else {
            out += ' ';
        }
    }
    return " " + out + " ";
}

bool has(const std::string& folded, const char* word) {
    return folded.find(std::string(" ") + word + " ") != std::string::npos;
}

bool hasPart(const std::string& folded, const char* part) { return folded.find(part) != std::string::npos; }

}  // namespace

std::vector<jellyfin::MediaSegment> segmentsFromChapters(const std::vector<jellyfin::Chapter>& chapters,
                                                         std::int64_t runTicks) {
    std::vector<jellyfin::MediaSegment> out;
    if (runTicks <= 0) {
        return out;
    }
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        const std::int64_t start = chapters[i].startTicks;
        const std::int64_t end = i + 1 < chapters.size() ? chapters[i + 1].startTicks : runTicks;
        if (start < 0 || end <= start || start >= runTicks) {
            continue;
        }
        const std::string name = folded(chapters[i].name);
        const bool firstHalf = start < runTicks / 2;
        const bool either = hasPart(name, "generique") || hasPart(name, "abspann") || hasPart(name, "vorspann");
        const bool intro = has(name, "intro") || has(name, "introduction") || has(name, "opening") ||
                           has(name, "op") || hasPart(name, "recap") || has(name, "previously") ||
                           hasPart(name, "resume") || has(name, "cold open") || has(name, "ouverture");
        const bool credits = hasPart(name, "credit") || has(name, "ending") || has(name, "ed") ||
                             has(name, "outro") || has(name, "end") || has(name, "fin");
        jellyfin::SegmentType type = jellyfin::SegmentType::Other;
        if ((intro || either) && firstHalf) {
            type = jellyfin::SegmentType::Intro;
        } else if ((credits || either) && !firstHalf) {
            type = jellyfin::SegmentType::Outro;
        }
        if (type == jellyfin::SegmentType::Other) {
            continue;
        }
        if (!out.empty() && out.back().type == type && out.back().endTicks == start) {
            out.back().endTicks = end;  // a recap then an opening: one intro
        } else {
            out.push_back({type, start, end});
        }
    }
    return out;
}

SegmentButton buttonAt(double position, const std::vector<jellyfin::MediaSegment>& segments) {
    for (const jellyfin::MediaSegment& segment : segments) {
        const double start = static_cast<double>(segment.startTicks) / kTicksPerSecond;
        const double end = static_cast<double>(segment.endTicks) / kTicksPerSecond;
        if (position < start || position >= end) {
            continue;
        }
        if (segment.type == jellyfin::SegmentType::Intro) {
            return {SegmentButton::SkipIntro, end};
        }
        if (segment.type == jellyfin::SegmentType::Outro) {
            return {SegmentButton::SkipOutro, end};
        }
    }
    return {};
}

NextCard::View NextCard::at(double position, double duration, bool hasNext) {
    const double left = duration - position;
    if (!hasNext || duration <= 0 || left > kNextCardSeconds) {
        if (left > kNextCardSeconds) {
            cancelled_ = false;  // sought back: the card may come again
        }
        return {};
    }
    if (cancelled_) {
        return {};
    }
    View view;
    view.shown = true;
    view.seconds = left <= 0 ? 0 : static_cast<int>(left + 0.999);
    return view;
}

SegmentButton ButtonTimer::visible(const SegmentButton& wanted, std::int64_t nowMs) {
    if (wanted.kind != kind_ || wanted.seekTo != seekTo_) {
        kind_ = wanted.kind;
        seekTo_ = wanted.seekTo;
        sinceMs_ = nowMs;
    }
    if (wanted.kind == SegmentButton::None || nowMs - sinceMs_ > kButtonMs) {
        return {};
    }
    return wanted;
}

void sendReport(const jellyfin::Client& client, ReportKind kind, const jellyfin::PlaybackState& state) {
    const core::Result<core::Unit> result = kind == ReportKind::Start      ? client.reportStart(state)
                                            : kind == ReportKind::Progress ? client.reportProgress(state)
                                                                           : client.reportStopped(state);
    if (!result.ok()) {
        PF_LOG_DEBUG("player: report failed: %s", result.error().message.c_str());
    }
}

}  // namespace playfin::player
