#include <cstddef>
#include <cstdint>
#include <string_view>

#include "jellyfin/xmltv.hpp"

// The guide in pieces of the size given by the first byte (as it would arrive), plus its times and channel keys.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::size_t piece = 1 + data[0] % 64;
    const char* text = reinterpret_cast<const char*>(data + 1);
    const std::size_t length = size - 1;
    playfin::jellyfin::XmltvReader reader(0, 4102444800);  // up to 2100
    for (std::size_t i = 0; i < length; i += piece) {
        reader.feed(text + i, length - i < piece ? length - i : piece);
    }
    const auto guide = reader.finish();
    for (const auto& channel : guide.channels) {
        for (const auto& name : channel.names) {
            (void)playfin::jellyfin::channelKey(name);
        }
    }
    (void)playfin::jellyfin::parseXmltvTime(std::string_view(text, length));
    return 0;
}
