#include <cstddef>
#include <cstdint>
#include <string_view>

#include "net/url.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)playfin::net::candidateUrls(text);
    if (const auto url = playfin::net::parseUrl(text)) {
        (void)playfin::net::toString(*url);
    }
    (void)playfin::net::urlEncode(text);
    return 0;
}
