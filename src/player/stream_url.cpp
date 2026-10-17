#include "player/stream_url.hpp"

#include <cctype>

#include "net/url.hpp"

namespace playfin::player {

std::string directStreamUrl(const jellyfin::Session& session, const std::string& itemId,
                            const std::string& mediaSourceId, const std::string& playSessionId, MediaKind kind) {
    net::Query query;
    query.add("static", "true");
    if (!mediaSourceId.empty()) {
        query.add("mediaSourceId", mediaSourceId);
    }
    if (!playSessionId.empty()) {
        query.add("playSessionId", playSessionId);
    }
    return session.baseUrl + (kind == MediaKind::Audio ? "/Audio/" : "/Videos/") + itemId + "/stream" + query.str();
}

namespace {

bool isApiKey(const std::string& parameter) {
    const std::size_t equals = parameter.find('=');
    std::string name = parameter.substr(0, equals);
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name == "api_key" || name == "apikey";
}

}  // namespace

std::string withoutApiKey(const std::string& url) {
    const std::size_t question = url.find('?');
    if (question == std::string::npos) {
        return url;
    }
    std::string kept;
    std::size_t start = question + 1;
    while (start <= url.size()) {
        std::size_t end = url.find('&', start);
        if (end == std::string::npos) {
            end = url.size();
        }
        const std::string parameter = url.substr(start, end - start);
        if (!parameter.empty() && !isApiKey(parameter)) {
            kept += (kept.empty() ? "" : "&") + parameter;
        }
        start = end + 1;
    }
    return url.substr(0, question) + (kept.empty() ? "" : "?" + kept);
}

}  // namespace playfin::player
