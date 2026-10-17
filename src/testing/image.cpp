#include "testing/image.hpp"

#include <stb/stb_image_write.h>
#include <stb_image.h>

#include <cstdlib>

namespace playfin::testing {

bool writePng(const std::string& path, const Image& image) {
    return stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4) != 0;
}

std::optional<Image> loadPng(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        return std::nullopt;
    }
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(data);
    return image;
}

DiffResult diffImages(const Image& a, const Image& b, int tolerance) {
    DiffResult result;
    result.sameSize = a.width == b.width && a.height == b.height && a.rgba.size() == b.rgba.size();
    if (!result.sameSize) {
        return result;
    }
    result.total = a.rgba.size() / 4;
    for (std::size_t pixel = 0; pixel < result.total; ++pixel) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const int delta =
                static_cast<int>(a.rgba[pixel * 4 + channel]) - static_cast<int>(b.rgba[pixel * 4 + channel]);
            if (std::abs(delta) > tolerance) {
                ++result.differing;
                break;
            }
        }
    }
    return result;
}

bool imagesMatch(const DiffResult& diff, double maxRatio) {
    if (!diff.sameSize || diff.total == 0) {
        return false;
    }
    return static_cast<double>(diff.differing) <= maxRatio * static_cast<double>(diff.total);
}

}  // namespace playfin::testing
