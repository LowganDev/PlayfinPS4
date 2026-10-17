#pragma once

#include <string>

#include "jellyfin/client.hpp"

namespace playfin::player {

enum class MediaKind { Video, Audio };

// <base>/Videos/<id>/stream?static=true[&mediaSourceId=…][&playSessionId=…] (/Audio/ for music): the file as it
// is. The token is sent in the Authorization header, never in the URL.
std::string directStreamUrl(const jellyfin::Session& session, const std::string& itemId,
                            const std::string& mediaSourceId, const std::string& playSessionId,
                            MediaKind kind = MediaKind::Video);

// The URL without its api_key query parameters (any case): servers add the token to transcoding URLs.
std::string withoutApiKey(const std::string& url);

}  // namespace playfin::player
