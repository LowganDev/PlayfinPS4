#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace playfin::net {

// A server address: http(s) only, no user info, query or fragment. Lower-case scheme and host, path without the
// trailing slash ("" or "/sub/path"), port 0 when absent.
struct Url {
    std::string scheme;
    std::string host;
    int port = 0;
    std::string path;
};

std::optional<Url> parseUrl(std::string_view text);
std::string toString(const Url& url);
std::string origin(const Url& url);  // scheme://host[:port]

// What to try, in order, for an address typed by the user.
// Empty when the input cannot be a server address.
std::vector<std::string> candidateUrls(std::string_view input);

// RFC 3986: unreserved characters kept, everything else %XX.
std::string urlEncode(std::string_view text);

// "?a=1&b=2" (empty when nothing was added).
class Query {
public:
    Query& add(std::string_view key, std::string_view value);
    Query& add(std::string_view key, long long value);
    const std::string& str() const { return text_; }

private:
    std::string text_;
};

}  // namespace playfin::net
