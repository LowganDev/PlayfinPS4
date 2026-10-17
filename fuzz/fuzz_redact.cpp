#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/redact.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    (void)playfin::core::redact(std::string_view(reinterpret_cast<const char*>(data), size));
    return 0;
}
