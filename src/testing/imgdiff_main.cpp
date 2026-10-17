#include <cstdio>

#include "testing/image.hpp"

// playfin_imgdiff <reference.png> <actual.png>: exit 0 when they match
int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: playfin_imgdiff <reference.png> <actual.png>\n");
        return 2;
    }
    const auto reference = playfin::testing::loadPng(argv[1]);
    const auto actual = playfin::testing::loadPng(argv[2]);
    if (!reference || !actual) {
        std::fprintf(stderr, "cannot read %s\n", reference ? argv[2] : argv[1]);
        return 1;
    }
    const auto diff = playfin::testing::diffImages(*reference, *actual);
    const bool match = playfin::testing::imagesMatch(diff);
    if (!diff.sameSize) {
        std::fprintf(stderr, "%s: size %dx%d, expected %dx%d\n", argv[2], actual->width, actual->height,
                     reference->width, reference->height);
    } else {
        std::fprintf(match ? stdout : stderr, "%s: %zu of %zu pixels differ\n", argv[2], diff.differing, diff.total);
    }
    return match ? 0 : 1;
}
