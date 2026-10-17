#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "render/raster.hpp"

// Random paints, states and triangles on a small surface: no out-of-bounds access, no hang, no UB.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace playfin::render;
    if (size < 8) {
        return 0;
    }
    std::size_t offset = 8;
    auto next = [&]() {
        float value = 0;
        if (offset + 4 <= size) {
            std::memcpy(&value, data + offset, 4);
            offset += 4;
        }
        return value;
    };
    Texture texture;
    texture.width = 3;
    texture.height = 2;
    texture.alphaOnly = (data[0] & 1) != 0;
    texture.repeatX = (data[0] & 2) != 0;
    texture.repeatY = (data[0] & 4) != 0;
    texture.nearest = (data[0] & 8) != 0;
    texture.texels.assign(texture.alphaOnly ? 6u : 24u, 0x80);
    Paint paint;
    paint.type = static_cast<PaintType>(data[1] % 4);
    paint.texType = static_cast<TexType>(data[2] % 3);
    paint.texture = (data[0] & 16) != 0 ? &texture : nullptr;
    for (float& v : paint.scissorMat) {
        v = next();
    }
    for (float& v : paint.paintMat) {
        v = next();
    }
    for (float& v : paint.innerCol) {
        v = next();
    }
    for (float& v : paint.outerCol) {
        v = next();
    }
    paint.extent[0] = next();
    paint.extent[1] = next();
    paint.radius = next();
    paint.feather = next();
    paint.strokeMult = next();
    paint.strokeThr = next();
    DrawState state;
    state.writeColor = (data[3] & 1) != 0;
    state.test = static_cast<StencilTest>((data[3] >> 1) % 3);
    state.fail = static_cast<StencilOp>(data[4] % 5);
    state.passFront = static_cast<StencilOp>((data[4] >> 3) % 5);
    state.passBack = static_cast<StencilOp>(data[5] % 5);
    state.cull = (data[5] & 0x80) != 0 ? Cull::Back : Cull::None;
    std::vector<Vertex> vertices;
    while (offset + 16 <= size && vertices.size() < 64) {
        vertices.push_back({next(), next(), next(), next()});
    }
    Surface surface(32, 32);
    surface.clear(0, 0, 0, 255);
    draw(surface, state, paint, (data[6] & 1) != 0, static_cast<Primitive>(data[7] % 3), vertices.data(),
         static_cast<int>(vertices.size()));
    return 0;
}
