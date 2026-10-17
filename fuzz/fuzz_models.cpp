#include <cstddef>
#include <cstdint>
#include <string_view>

#include "jellyfin/models.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    using namespace playfin::jellyfin;
    (void)parseItemPage(text);
    (void)parseItem(text);
    (void)parseServerInfo(text);
    (void)parsePublicUsers(text);
    (void)parseUser(text);
    (void)parseAuth(text);
    (void)parseQuickConnect(text);
    (void)parseBool(text);
    (void)parseVersion(text);
    (void)parsePlaybackInfo(text);
    (void)parseMediaSegments(text);
    (void)parseTimers(text);
    (void)parseUtc(text);
    return 0;
}
