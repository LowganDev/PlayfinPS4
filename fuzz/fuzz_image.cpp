#include <cstddef>
#include <cstdint>
#include <string>

#include "ui/images.hpp"

// Pictures come from the server: any bytes must decode or be refused, never crash.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string bytes(reinterpret_cast<const char*>(data), size);
    (void)playfin::ui::decodeImage(bytes, 64, 64, playfin::ui::ImageShape::Whole);
    (void)playfin::ui::decodeImage(bytes, 32, 48, playfin::ui::ImageShape::Crop);
    return 0;
}
