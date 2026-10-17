#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace playfin::testing {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // top-down rows
};

bool writePng(const std::string& path, const Image& image);
std::optional<Image> loadPng(const std::string& path);

struct DiffResult {
    bool sameSize = false;
    std::size_t differing = 0;  // pixels with a channel off by more than the tolerance
    std::size_t total = 0;
};

DiffResult diffImages(const Image& a, const Image& b, int tolerance = 8);

// Same size and at most maxRatio of the pixels differing (tolerates driver rounding).
bool imagesMatch(const DiffResult& diff, double maxRatio = 0.001);

}  // namespace playfin::testing
