#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace playfin::render {

// A CPU frame: RGBA8 with premultiplied alpha, top-down rows, and an 8-bit stencil (NanoVG fills and strokes).
struct Surface {
    Surface(int width, int height);
    void clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);  // colour, and the stencil to 0

    int width;
    int height;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> stencil;
};

// A sampled image. RGBA: 4 bytes per texel. Alpha-only (the font atlas): 1 byte, sampled as (L, L, L, 1) like the
// GLES2 luminance texture NanoVG uses.
struct Texture {
    int width = 0;
    int height = 0;
    bool alphaOnly = false;
    bool repeatX = false;
    bool repeatY = false;
    bool nearest = false;
    bool opaque = false;  // RGBA with every alpha at 255 (kept up to date by the owner): no blending needed
    std::vector<std::uint8_t> texels;
};

struct Vertex {
    float x;
    float y;
    float u;
    float v;
};

// The uniforms of NanoVG's GLES2 fragment shader (nanovg_gl.h), evaluated per pixel.
enum class PaintType { Gradient = 0, Image = 1, StencilOnly = 2, TexturedTriangles = 3 };
enum class TexType { Premultiplied = 0, Straight = 1, Alpha = 2 };

struct Paint {
    // affine transforms (a, b, c, d, e, f): x' = a*x + c*y + e, y' = b*x + d*y + f
    float scissorMat[6] = {0, 0, 0, 0, 0, 0};  // all zero with extent and scale 1: no scissor
    float paintMat[6] = {0, 0, 0, 0, 0, 0};
    float innerCol[4] = {0, 0, 0, 0};  // premultiplied
    float outerCol[4] = {0, 0, 0, 0};
    float scissorExt[2] = {1, 1};
    float scissorScale[2] = {1, 1};
    float extent[2] = {0, 0};
    float radius = 0;
    float feather = 1;
    float strokeMult = 1;
    float strokeThr = -1;  // fragments whose edge alpha is lower are discarded (no colour, no stencil change)
    TexType texType = TexType::Premultiplied;
    PaintType type = PaintType::StencilOnly;
    const Texture* texture = nullptr;
};

// ConvexPolygon: a convex polygon given like a fan (NanoVG's convex fills), drawn as the union of its fan triangles
// with one span per row; vertices with different u/v fall back to the fan.
enum class Primitive { Triangles, Strip, Fan, ConvexPolygon };
enum class StencilTest { Always, EqualZero, NotEqualZero };
enum class StencilOp { Keep, Incr, IncrWrap, DecrWrap, Zero };
enum class Cull { None, Back };

// GL state of one draw. Front faces turn counter-clockwise as seen on screen (GL_CCW with NanoVG's y flip).
struct DrawState {
    bool writeColor = true;
    StencilTest test = StencilTest::Always;
    StencilOp fail = StencilOp::Keep;       // the stencil test failed
    StencilOp passFront = StencilOp::Keep;  // passed, front-facing triangle
    StencilOp passBack = StencilOp::Keep;   // passed, back-facing triangle
    Cull cull = Cull::Back;
};

// Draws like GLES2: pixel-centre sampling with the top-left rule, affine u/v interpolation, NanoVG's fragment
// shader (edgeAA: its EDGE_AA variant, which may discard), then the stencil test/operation and premultiplied
// source-over blending. Triangles with non-finite coordinates are skipped; everything is clipped to the surface.
void draw(Surface& surface, const DrawState& state, const Paint& paint, bool edgeAA, Primitive primitive,
          const Vertex* vertices, int count);

// Draws only the rows [rowBegin, rowEnd) of a surface, with its own caches: threads drawing disjoint bands of one
// surface at the same time produce exactly the frame a single draw would.
class Band {
public:
    Band(int rowBegin, int rowEnd);
    ~Band();
    Band(const Band&) = delete;
    Band& operator=(const Band&) = delete;

    void draw(Surface& surface, const DrawState& state, const Paint& paint, bool edgeAA, Primitive primitive,
              const Vertex* vertices, int count);

private:
    struct Cache;
    int rowBegin_;
    int rowEnd_;
    std::unique_ptr<Cache> cache_;
};

// Diagnostics: what happened to the triangles since the last reset (counted from every thread).
struct RasterStats {
    long triangles = 0;    // reached the rasterizer
    long nonFinite = 0;    // skipped: NaN or infinite coordinates
    long degenerate = 0;   // skipped: zero area
    long culled = 0;       // skipped: back face
    long offRows = 0;      // outside the rows being drawn
    long rows = 0;         // rows with a non-empty span
    long pixels = 0;       // pixels that passed the stencil test
    long shaded = 0;       // of those: through the general per-pixel shader
    long shadedText = 0;   //   of which textured triangles (text)
    long shadedImage = 0;  //   of which image paints
    long imaged = 0;       // through the image row path
    long copied = 0;       //   of which copied row by row (opaque 1:1 images)
};
RasterStats rasterStats();
void resetRasterStats();

// Tests: turn the fast paths (constant colour, image, text, glyph quads) off to compare them with the general path.
void setFastPathsForTesting(bool enabled);

}  // namespace playfin::render
