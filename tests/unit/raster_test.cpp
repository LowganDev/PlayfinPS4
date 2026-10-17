#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <vector>

#include "render/raster.hpp"

using namespace playfin::render;

namespace {

Paint solid(float r, float g, float b, float a) {
    Paint p;
    p.type = PaintType::Gradient;
    p.paintMat[0] = 1;
    p.paintMat[3] = 1;
    const float color[4] = {r * a, g * a, b * a, a};
    for (int i = 0; i < 4; ++i) {
        p.innerCol[i] = color[i];
        p.outerCol[i] = color[i];
    }
    p.feather = 1;
    return p;
}

const std::uint8_t* pixel(const Surface& s, int x, int y) {
    return &s.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) + static_cast<std::size_t>(x)) * 4];
}

int covered(const Surface& s) {
    int count = 0;
    for (std::size_t i = 0; i < s.rgba.size(); i += 4) {
        count += s.rgba[i + 3] != 0 ? 1 : 0;
    }
    return count;
}

DrawState noCull() {
    DrawState state;
    state.cull = Cull::None;
    return state;
}

// two triangles covering (x0, y0)-(x1, y1), sharing the diagonal
std::vector<Vertex> rect(float x0, float y0, float x1, float y1) {
    return {{x0, y0, 0.5f, 1}, {x1, y0, 0.5f, 1}, {x1, y1, 0.5f, 1},
            {x0, y0, 0.5f, 1}, {x1, y1, 0.5f, 1}, {x0, y1, 0.5f, 1}};
}

}  // namespace

TEST_CASE("pixels are covered when their centre is inside, shared edges are drawn once") {
    Surface s(8, 8);
    s.clear(0, 0, 0, 0);
    const auto quad = rect(2, 2, 6, 6);
    draw(s, noCull(), solid(1, 0, 0, 0.5f), false, Primitive::Triangles, quad.data(), 6);
    CHECK(covered(s) == 16);
    for (int y = 2; y < 6; ++y) {
        for (int x = 2; x < 6; ++x) {
            INFO(x << "," << y);
            CHECK(pixel(s, x, y)[0] == 128);  // drawn once: a second blend would give 191
            CHECK(pixel(s, x, y)[3] == 128);
        }
    }
}

TEST_CASE("centres exactly on an edge follow the top-left rule") {
    Surface s(8, 8);
    s.clear(0, 0, 0, 0);
    const auto quad = rect(2.5f, 2.5f, 5.5f, 5.5f);
    draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, quad.data(), 6);
    CHECK(covered(s) == 9);
    CHECK(pixel(s, 2, 2)[3] == 255);  // left and top edges are inside
    CHECK(pixel(s, 5, 4)[3] == 0);    // right edge is outside
    CHECK(pixel(s, 4, 5)[3] == 0);    // bottom edge is outside
}

TEST_CASE("back faces are dropped only when culling") {
    Surface s(8, 8);
    s.clear(0, 0, 0, 0);
    const Vertex clockwise[3] = {{2, 2, 0, 0}, {6, 2, 0, 0}, {6, 6, 0, 0}};  // as seen on screen: back face
    DrawState cull;
    draw(s, cull, solid(1, 1, 1, 1), false, Primitive::Triangles, clockwise, 3);
    CHECK(covered(s) == 0);
    const Vertex counterClockwise[3] = {{2, 2, 0, 0}, {6, 6, 0, 0}, {6, 2, 0, 0}};
    draw(s, cull, solid(1, 1, 1, 1), false, Primitive::Triangles, counterClockwise, 3);
    CHECK(covered(s) > 0);
    s.clear(0, 0, 0, 0);
    draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, clockwise, 3);
    CHECK(covered(s) > 0);
}

TEST_CASE("strips and fans assemble triangles like GL") {
    Surface s(8, 8);
    s.clear(0, 0, 0, 0);
    // strip: top-left, top-right, bottom-left, bottom-right; GL swaps odd triangles, so both face the same way
    const Vertex strip[4] = {{1, 1, 0, 0}, {7, 1, 0, 0}, {1, 7, 0, 0}, {7, 7, 0, 0}};
    DrawState cullNone = noCull();
    draw(s, cullNone, solid(1, 1, 1, 0.5f), false, Primitive::Strip, strip, 4);
    CHECK(covered(s) == 36);
    CHECK(pixel(s, 4, 4)[3] == 128);
    s.clear(0, 0, 0, 0);
    const Vertex fan[4] = {{1, 1, 0, 0}, {1, 7, 0, 0}, {7, 7, 0, 0}, {7, 1, 0, 0}};
    draw(s, cullNone, solid(1, 1, 1, 0.5f), false, Primitive::Fan, fan, 4);
    CHECK(covered(s) == 36);
    CHECK(pixel(s, 4, 4)[3] == 128);
}

TEST_CASE("two-sided stencil counts winding; the cover pass paints non-zero pixels and clears the stencil") {
    Surface s(8, 8);
    s.clear(0, 0, 0, 255);
    const Vertex outer[4] = {{1, 1, 0.5f, 1}, {7, 1, 0.5f, 1}, {7, 7, 0.5f, 1}, {1, 7, 0.5f, 1}};
    const Vertex hole[4] = {{3, 3, 0.5f, 1}, {3, 5, 0.5f, 1}, {5, 5, 0.5f, 1}, {5, 3, 0.5f, 1}};
    DrawState winding;
    winding.writeColor = false;
    winding.cull = Cull::None;
    winding.passFront = StencilOp::IncrWrap;
    winding.passBack = StencilOp::DecrWrap;
    Paint stencilOnly;
    draw(s, winding, stencilOnly, false, Primitive::Fan, outer, 4);
    draw(s, winding, stencilOnly, false, Primitive::Fan, hole, 4);
    CHECK(s.stencil[static_cast<std::size_t>(4 * 8 + 4)] == 0);
    CHECK(s.stencil[static_cast<std::size_t>(1 * 8 + 1)] != 0);
    DrawState cover;
    cover.cull = Cull::None;
    cover.test = StencilTest::NotEqualZero;
    cover.fail = StencilOp::Zero;
    cover.passFront = StencilOp::Zero;
    cover.passBack = StencilOp::Zero;
    const Vertex all[4] = {{0, 0, 0.5f, 1}, {8, 0, 0.5f, 1}, {0, 8, 0.5f, 1}, {8, 8, 0.5f, 1}};
    draw(s, cover, solid(1, 1, 1, 1), false, Primitive::Strip, all, 4);
    CHECK(pixel(s, 1, 1)[0] == 255);
    CHECK(pixel(s, 6, 6)[0] == 255);
    CHECK(pixel(s, 4, 4)[0] == 0);  // the hole
    CHECK(pixel(s, 0, 0)[0] == 0);  // outside
    for (std::uint8_t value : s.stencil) {
        CHECK(value == 0);
    }
}

TEST_CASE("blending is premultiplied source-over") {
    Surface s(1, 1);
    s.clear(0, 0, 255, 255);
    const Vertex tri[3] = {{0, 0, 0.5f, 1}, {0, 2, 0.5f, 1}, {2, 0, 0.5f, 1}};
    draw(s, noCull(), solid(1, 0, 0, 0.5f), false, Primitive::Triangles, tri, 3);
    CHECK(pixel(s, 0, 0)[0] == 128);
    CHECK(pixel(s, 0, 0)[1] == 0);
    CHECK(pixel(s, 0, 0)[2] == 128);
    CHECK(pixel(s, 0, 0)[3] == 255);
}

TEST_CASE("image paint samples the texture through the paint transform") {
    Texture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    for (bool nearest : {true, false}) {
        texture.nearest = nearest;
        Paint p;
        p.type = PaintType::Image;
        p.texType = TexType::Straight;
        p.paintMat[0] = 1;
        p.paintMat[3] = 1;
        p.extent[0] = 2;
        p.extent[1] = 2;
        p.innerCol[0] = p.innerCol[1] = p.innerCol[2] = p.innerCol[3] = 1;
        p.texture = &texture;
        Surface s(2, 2);
        s.clear(0, 0, 0, 255);
        const auto quad = rect(0, 0, 2, 2);
        draw(s, noCull(), p, false, Primitive::Triangles, quad.data(), 6);
        INFO("nearest " << nearest);
        CHECK(pixel(s, 0, 0)[0] == 255);
        CHECK(pixel(s, 1, 0)[1] == 255);
        CHECK(pixel(s, 0, 1)[2] == 255);
        CHECK(pixel(s, 1, 1)[0] == 255);
        CHECK(pixel(s, 1, 1)[1] == 255);
    }
}

TEST_CASE("textured triangles tint an alpha texture (text)") {
    Texture atlas;
    atlas.width = 1;
    atlas.height = 1;
    atlas.alphaOnly = true;
    atlas.texels = {128};
    Paint p;
    p.type = PaintType::TexturedTriangles;
    p.texType = TexType::Alpha;
    p.innerCol[1] = 1;
    p.innerCol[3] = 1;
    p.texture = &atlas;
    Surface s(4, 4);
    s.clear(0, 0, 0, 0);
    const Vertex tri[3] = {{0, 0, 0.5f, 0.5f}, {0, 4, 0.5f, 0.5f}, {4, 0, 0.5f, 0.5f}};
    draw(s, noCull(), p, true, Primitive::Triangles, tri, 3);
    CHECK(pixel(s, 0, 0)[0] == 0);
    CHECK(pixel(s, 0, 0)[1] == 128);
    CHECK(pixel(s, 0, 0)[3] == 128);
}

TEST_CASE("scissor fades over half a pixel at its edges") {
    Surface s(8, 1);
    s.clear(0, 0, 0, 0);
    Paint p = solid(1, 1, 1, 1);
    // scissor box centred on x = 4 with half-width 2.25: inverse transform is a translation by -4
    p.scissorMat[0] = 1;
    p.scissorMat[3] = 1;
    p.scissorMat[4] = -4;
    p.scissorMat[5] = -0.5f;
    p.scissorExt[0] = 2.25f;
    p.scissorExt[1] = 10;
    const auto quad = rect(0, 0, 8, 1);
    draw(s, noCull(), p, false, Primitive::Triangles, quad.data(), 6);
    CHECK(pixel(s, 0, 0)[3] == 0);
    CHECK(pixel(s, 1, 0)[3] == 64);
    CHECK(pixel(s, 2, 0)[3] == 255);
    CHECK(pixel(s, 5, 0)[3] == 255);
    CHECK(pixel(s, 6, 0)[3] == 64);
    CHECK(pixel(s, 7, 0)[3] == 0);
}

TEST_CASE("edge anti-aliasing comes from u/v; fragments below the threshold are discarded") {
    Surface s(2, 2);
    s.clear(0, 0, 0, 0);
    Paint p = solid(1, 1, 1, 1);
    p.strokeMult = 1;
    const Vertex half[3] = {{0, 0, 0.25f, 1}, {0, 4, 0.25f, 1}, {4, 0, 0.25f, 1}};  // (1 - |0.25*2 - 1|) = 0.5
    draw(s, noCull(), p, true, Primitive::Triangles, half, 3);
    CHECK(pixel(s, 0, 0)[3] == 128);
    s.clear(0, 0, 0, 0);
    p.strokeThr = 0.6f;
    DrawState incr = noCull();
    incr.passFront = StencilOp::Incr;
    incr.passBack = StencilOp::Incr;
    draw(s, incr, p, true, Primitive::Triangles, half, 3);
    CHECK(pixel(s, 0, 0)[3] == 0);
    CHECK(s.stencil[0] == 0);  // discarded: no stencil update either
}

TEST_CASE("non-finite and huge coordinates never write out of bounds") {
    Surface s(4, 4);
    s.clear(0, 0, 0, 0);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Vertex bad[6] = {{nan, 0, 0, 0}, {4, 4, 0, 0}, {0, 4, 0, 0}, {inf, -inf, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}};
    draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, bad, 6);
    CHECK(covered(s) == 0);
    const Vertex huge[3] = {{-1.0e9f, -1.0e9f, 0, 0}, {3.0e9f, -1.0e9f, 0, 0}, {-1.0e9f, 3.0e9f, 0, 0}};
    draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, huge, 3);
    CHECK(covered(s) == 16);
    draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, huge, 2);  // too few vertices
    Surface empty(0, 0);
    draw(empty, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, huge, 3);
}

TEST_CASE("the constant-colour fast path draws exactly what the general path draws") {
    const Vertex shapes[] = {{1.2f, 0.7f, 0.5f, 1}, {30.4f, 3.1f, 0.5f, 1}, {12.9f, 28.6f, 0.5f, 1},
                             {5, 5, 0.25f, 1},      {25, 9, 0.25f, 1},     {9, 25, 0.25f, 1}};
    for (bool edgeAA : {false, true}) {
        Paint p = solid(0.2f, 0.6f, 0.9f, 0.7f);
        p.strokeMult = 1;
        Surface fast(32, 32);
        Surface general(32, 32);
        fast.clear(10, 20, 30, 255);
        general.clear(10, 20, 30, 255);
        DrawState state = noCull();
        state.passFront = StencilOp::IncrWrap;
        state.passBack = StencilOp::IncrWrap;
        setFastPathsForTesting(true);
        draw(fast, state, p, edgeAA, Primitive::Triangles, shapes, 6);
        setFastPathsForTesting(false);
        draw(general, state, p, edgeAA, Primitive::Triangles, shapes, 6);
        setFastPathsForTesting(true);
        INFO("edgeAA " << edgeAA);
        CHECK(fast.rgba == general.rgba);
        CHECK(fast.stencil == general.stencil);
    }
}

TEST_CASE("coverage matches the pixel-centre rule exactly for many random triangles") {
    // the rule, computed independently: E(P) = (q - p) x (P - p) on a positive-area triangle, ties to top-left edges
    auto edge = [](const Vertex& p, const Vertex& q, double px, double py, bool& owns) {
        const double dx = static_cast<double>(q.x) - static_cast<double>(p.x);
        const double dy = static_cast<double>(q.y) - static_cast<double>(p.y);
        owns = dy < 0 || (dy == 0 && dx > 0);
        return -dy * px + dx * py + (dy * static_cast<double>(p.x) - dx * static_cast<double>(p.y));
    };
    std::uint32_t seed = 12345;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
    };
    for (int round = 0; round < 300; ++round) {
        Vertex a{next() * 80 - 8, next() * 80 - 8, 0.5f, 1};
        Vertex b{next() * 80 - 8, next() * 80 - 8, 0.5f, 1};
        Vertex c{next() * 80 - 8, next() * 80 - 8, 0.5f, 1};
        if (round % 3 == 0) {  // half-pixel positions hit pixel centres exactly
            a.x = static_cast<float>(static_cast<int>(a.x)) + 0.5f;
            b.y = static_cast<float>(static_cast<int>(b.y)) + 0.5f;
        }
        Surface s(64, 64);
        s.clear(0, 0, 0, 0);
        const Vertex tri[3] = {a, b, c};
        draw(s, noCull(), solid(1, 1, 1, 1), false, Primitive::Triangles, tri, 3);
        const double area = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                            (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
        Vertex p = a;
        Vertex q = area < 0 ? c : b;
        Vertex r = area < 0 ? b : c;
        int mismatches = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                bool o1 = false;
                bool o2 = false;
                bool o3 = false;
                const double px = x + 0.5;
                const double py = y + 0.5;
                const double e1 = edge(p, q, px, py, o1);
                const double e2 = edge(q, r, px, py, o2);
                const double e3 = edge(r, p, px, py, o3);
                const bool expected = area != 0 && (e1 > 0 || (e1 == 0 && o1)) && (e2 > 0 || (e2 == 0 && o2)) &&
                                      (e3 > 0 || (e3 == 0 && o3));
                const bool drawn = pixel(s, x, y)[3] != 0;
                mismatches += expected != drawn ? 1 : 0;
            }
        }
        INFO("round " << round);
        CHECK(mismatches == 0);
    }
}

TEST_CASE("fast paths inside a scissor draw exactly what the general path draws") {
    const Vertex shapes[] = {{6.2f, 5.7f, 0.5f, 1}, {25.4f, 7.1f, 0.5f, 1}, {12.9f, 24.6f, 0.5f, 1},
                             {0, 0, 0.5f, 1},       {31, 2, 0.5f, 1},      {3, 31, 0.5f, 1}};
    Paint p = solid(0.9f, 0.3f, 0.1f, 0.8f);
    p.scissorMat[0] = 1;
    p.scissorMat[3] = 1;
    p.scissorMat[4] = -16;
    p.scissorMat[5] = -16;
    p.scissorExt[0] = 12;
    p.scissorExt[1] = 12;
    Surface fast(32, 32);
    Surface general(32, 32);
    fast.clear(10, 20, 30, 255);
    general.clear(10, 20, 30, 255);
    setFastPathsForTesting(true);
    draw(fast, noCull(), p, true, Primitive::Triangles, shapes, 6);  // inside the scissor, then crossing it
    setFastPathsForTesting(false);
    draw(general, noCull(), p, true, Primitive::Triangles, shapes, 6);
    setFastPathsForTesting(true);
    CHECK(fast.rgba == general.rgba);
}

// The image fast path filters with integer weights (1/256, like GPU filtering): within 2 per channel of the float
// general path. Pixels on the scissor's edges go through the general path.
TEST_CASE("the image fast path draws what the general path draws, within 2 per channel") {
    Texture texture;
    texture.width = 5;
    texture.height = 3;
    for (int i = 0; i < 15; ++i) {
        const auto v = static_cast<std::uint8_t>(i * 17);
        texture.texels.insert(texture.texels.end(), {v, static_cast<std::uint8_t>(255 - v), 90, static_cast<std::uint8_t>(i % 2 == 0 ? 200 : 255)});
    }
    const Vertex quad[6] = {{3, 2, 0.5f, 1}, {29, 2, 0.5f, 1}, {29, 27, 0.5f, 1},
                            {3, 2, 0.5f, 1}, {29, 27, 0.5f, 1}, {3, 27, 0.5f, 1}};
    for (bool nearest : {false, true}) {
        for (int scissor : {0, 1, 2}) {  // none, around the quad, crossing it
            for (TexType type : {TexType::Straight, TexType::Premultiplied}) {
                texture.nearest = nearest;
                Paint p;
                p.type = PaintType::Image;
                p.texType = type;
                p.paintMat[0] = 0.83f;  // scaled and moved, not rotated
                p.paintMat[3] = 1.21f;
                p.paintMat[4] = -2.5f;
                p.paintMat[5] = -1.75f;
                p.extent[0] = 21;
                p.extent[1] = 30;
                p.innerCol[0] = p.innerCol[1] = p.innerCol[2] = p.innerCol[3] = 0.9f;
                p.texture = &texture;
                if (scissor > 0) {
                    p.scissorMat[0] = 1;
                    p.scissorMat[3] = 1;
                    p.scissorMat[4] = -16;
                    p.scissorMat[5] = -16;
                    p.scissorExt[0] = scissor == 1 ? 15 : 9.3f;
                    p.scissorExt[1] = scissor == 1 ? 15 : 7.6f;
                }
                Surface fast(32, 32);
                Surface general(32, 32);
                fast.clear(10, 20, 30, 255);
                general.clear(10, 20, 30, 255);
                setFastPathsForTesting(true);
                draw(fast, noCull(), p, true, Primitive::Triangles, quad, 6);
                setFastPathsForTesting(false);
                draw(general, noCull(), p, true, Primitive::Triangles, quad, 6);
                setFastPathsForTesting(true);
                int worst = 0;
                for (std::size_t i = 0; i < fast.rgba.size(); ++i) {
                    worst = std::max(worst, std::abs(int(fast.rgba[i]) - int(general.rgba[i])));
                }
                INFO("nearest " << nearest << " scissor " << scissor << " type " << static_cast<int>(type));
                CHECK(worst <= 2);
                CHECK(fast.rgba != std::vector<std::uint8_t>(fast.rgba.size(), 0));
            }
        }
    }
}

TEST_CASE("a convex polygon draws exactly what its fan draws") {
    std::uint32_t seed = 12345;
    auto random = [&seed](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
    };
    Texture texture;
    texture.width = 4;
    texture.height = 4;
    for (int i = 0; i < 16; ++i) {
        texture.texels.insert(texture.texels.end(), {static_cast<std::uint8_t>(i * 16), 80, 200, 255});
    }
    for (int round = 0; round < 60; ++round) {
        // points on an ellipse, in angle order: convex; either orientation
        const int n = 3 + round % 30;
        const float cx = random(10, 54);
        const float cy = random(10, 54);
        const float rx = random(2, 40);
        const float ry = random(2, 40);
        const float start = random(0, 6.28f);
        const bool clockwise = round % 2 == 0;
        std::vector<Vertex> polygon;
        for (int i = 0; i < n; ++i) {
            const float angle = start + (clockwise ? -1.0f : 1.0f) * 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            polygon.push_back({cx + rx * std::cos(angle), cy + ry * std::sin(angle), 0.5f, 1});
        }
        Paint p = solid(0.3f, 0.7f, 0.2f, round % 3 == 0 ? 1.0f : 0.6f);
        if (round % 4 == 1) {
            p.type = PaintType::Image;
            p.texType = TexType::Straight;
            p.paintMat[0] = 0.5f;
            p.paintMat[3] = 0.5f;
            p.extent[0] = 4;
            p.extent[1] = 4;
            p.innerCol[0] = p.innerCol[1] = p.innerCol[2] = p.innerCol[3] = 1;
            p.texture = &texture;
        }
        if (round % 5 == 2) {  // a scissor crossing the polygon
            p.scissorMat[0] = 1;
            p.scissorMat[3] = 1;
            p.scissorMat[4] = -32;
            p.scissorMat[5] = -30;
            p.scissorExt[0] = 17.3f;
            p.scissorExt[1] = 12.8f;
        }
        DrawState state = noCull();
        state.passFront = StencilOp::IncrWrap;
        state.passBack = StencilOp::IncrWrap;
        Surface polygonSurface(64, 64);
        Surface fanSurface(64, 64);
        polygonSurface.clear(10, 20, 30, 255);
        fanSurface.clear(10, 20, 30, 255);
        draw(polygonSurface, state, p, true, Primitive::ConvexPolygon, polygon.data(), n);
        draw(fanSurface, state, p, true, Primitive::Fan, polygon.data(), n);
        INFO("round " << round);
        CHECK(polygonSurface.rgba == fanSurface.rgba);
        CHECK(polygonSurface.stencil == fanSurface.stencil);
    }
}

TEST_CASE("opaque untinted images skip blending and stay within 2 per channel of the general path") {
    Texture texture;
    texture.width = 6;
    texture.height = 5;
    texture.opaque = true;
    for (int i = 0; i < 30; ++i) {
        texture.texels.insert(texture.texels.end(),
                              {static_cast<std::uint8_t>(i * 8), static_cast<std::uint8_t>(250 - i * 8), 77, 255});
    }
    const Vertex quad[6] = {{2.3f, 1.6f, 0.5f, 1}, {30.2f, 1.6f, 0.5f, 1}, {30.2f, 29.4f, 0.5f, 1},
                            {2.3f, 1.6f, 0.5f, 1}, {30.2f, 29.4f, 0.5f, 1}, {2.3f, 29.4f, 0.5f, 1}};
    Paint p;
    p.type = PaintType::Image;
    p.texType = TexType::Straight;
    p.paintMat[0] = 1;
    p.paintMat[3] = 1;
    p.paintMat[4] = -2.3f;
    p.paintMat[5] = -1.6f;
    p.extent[0] = 27.9f;
    p.extent[1] = 27.8f;
    p.innerCol[0] = p.innerCol[1] = p.innerCol[2] = p.innerCol[3] = 1;
    p.texture = &texture;
    Surface fast(32, 32);
    Surface general(32, 32);
    fast.clear(10, 20, 30, 255);
    general.clear(10, 20, 30, 255);
    setFastPathsForTesting(true);
    draw(fast, noCull(), p, true, Primitive::Triangles, quad, 6);
    setFastPathsForTesting(false);
    draw(general, noCull(), p, true, Primitive::Triangles, quad, 6);
    setFastPathsForTesting(true);
    int worst = 0;
    for (std::size_t i = 0; i < fast.rgba.size(); ++i) {
        worst = std::max(worst, std::abs(int(fast.rgba[i]) - int(general.rgba[i])));
    }
    CHECK(worst <= 2);
    CHECK(pixel(fast, 10, 10)[3] == 255);
}

TEST_CASE("glyph quads draw what their two triangles draw, within 2 per channel") {
    Texture atlas;
    atlas.width = 8;
    atlas.height = 8;
    atlas.alphaOnly = true;
    for (int i = 0; i < 64; ++i) {
        atlas.texels.push_back(static_cast<std::uint8_t>((i * 53) % 256));
    }
    std::vector<Vertex> quads;
    auto quad = [&quads](float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1) {
        quads.insert(quads.end(), {{x0, y0, s0, t0}, {x1, y1, s1, t1}, {x1, y0, s1, t0},
                                   {x0, y0, s0, t0}, {x0, y1, s0, t1}, {x1, y1, s1, t1}});
    };
    quad(1.3f, 2.6f, 12.8f, 19.1f, 0.0f, 0.0f, 0.5f, 1.0f);
    quad(14.5f, 3.5f, 30.5f, 11.5f, 0.5f, 0.25f, 1.0f, 0.75f);  // edges on pixel centres
    quad(4.0f, 20.0f, 28.7f, 31.2f, 0.1f, 0.6f, 0.9f, 0.95f);
    for (bool scissored : {false, true}) {
        Paint p;
        p.type = PaintType::TexturedTriangles;
        p.texType = TexType::Alpha;
        p.innerCol[0] = 0.8f;
        p.innerCol[1] = 0.6f;
        p.innerCol[2] = 0.3f;
        p.innerCol[3] = 0.9f;
        p.texture = &atlas;
        if (scissored) {
            p.scissorMat[0] = 1;
            p.scissorMat[3] = 1;
            p.scissorMat[4] = -16;
            p.scissorMat[5] = -16;
            p.scissorExt[0] = 11.2f;
            p.scissorExt[1] = 9.7f;
        }
        Surface fast(32, 32);
        Surface general(32, 32);
        fast.clear(10, 20, 30, 255);
        general.clear(10, 20, 30, 255);
        setFastPathsForTesting(true);
        draw(fast, DrawState{}, p, true, Primitive::Triangles, quads.data(), static_cast<int>(quads.size()));
        setFastPathsForTesting(false);
        draw(general, DrawState{}, p, true, Primitive::Triangles, quads.data(), static_cast<int>(quads.size()));
        setFastPathsForTesting(true);
        int worst = 0;
        for (std::size_t i = 0; i < fast.rgba.size(); ++i) {
            worst = std::max(worst, std::abs(int(fast.rgba[i]) - int(general.rgba[i])));
        }
        INFO("scissored " << scissored);
        CHECK(worst <= 2);
        CHECK(fast.rgba != std::vector<std::uint8_t>(fast.rgba.size(), 0));
    }
}

TEST_CASE("an opaque image drawn 1:1 on whole pixels copies its texels exactly") {
    Texture texture;
    texture.width = 8;
    texture.height = 6;
    texture.opaque = true;
    for (int i = 0; i < 48; ++i) {
        texture.texels.insert(texture.texels.end(), {static_cast<std::uint8_t>(i * 5), static_cast<std::uint8_t>(i * 3),
                                                     static_cast<std::uint8_t>(255 - i), 255});
    }
    const std::vector<Vertex> quad = rect(3, 2, 11, 8);
    Paint p;
    p.type = PaintType::Image;
    p.texType = TexType::Straight;
    p.paintMat[0] = 1;
    p.paintMat[3] = 1;
    p.paintMat[4] = -3;
    p.paintMat[5] = -2;
    p.extent[0] = 8;
    p.extent[1] = 6;
    p.innerCol[0] = p.innerCol[1] = p.innerCol[2] = p.innerCol[3] = 1;
    p.texture = &texture;
    Surface s(16, 12);
    s.clear(0, 0, 0, 255);
    draw(s, noCull(), p, true, Primitive::Triangles, quad.data(), 6);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::uint8_t* t = &texture.texels[static_cast<std::size_t>(y * 8 + x) * 4];
            const std::uint8_t* d = pixel(s, x + 3, y + 2);
            INFO("texel " << x << ", " << y);
            CHECK(std::equal(t, t + 4, d));
        }
    }
}
