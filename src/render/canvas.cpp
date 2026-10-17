#include "render/canvas.hpp"

#include <GLES2/gl2.h>
#include <nanovg.h>
#include <nanovg_gl.h>

#include <algorithm>
#include <cstddef>

namespace playfin::render {

NVGcontext* createCanvas() { return nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES); }

void destroyCanvas(NVGcontext* canvas) {
    if (canvas != nullptr) {
        nvgDeleteGLES2(canvas);
    }
}

std::vector<std::uint8_t> readFramebuffer(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    const auto rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (std::size_t top = 0, bottom = static_cast<std::size_t>(height) - 1; top < bottom; ++top, --bottom) {
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(top * rowBytes),
                         pixels.begin() + static_cast<std::ptrdiff_t>((top + 1) * rowBytes),
                         pixels.begin() + static_cast<std::ptrdiff_t>(bottom * rowBytes));
    }
    return pixels;
}

}  // namespace playfin::render
