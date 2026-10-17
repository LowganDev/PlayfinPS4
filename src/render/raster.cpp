#include "render/raster.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

namespace playfin::render {

namespace {

struct Color {
    float r;
    float g;
    float b;
    float a;
};

// the PS4 libc++ has no float overloads of the math functions: go through double
float absf(float v) { return v < 0 ? -v : v; }
float floorFloat(float v) { return static_cast<float>(std::floor(static_cast<double>(v))); }
float sqrtFloat(float v) { return static_cast<float>(std::sqrt(static_cast<double>(v))); }
bool finite(float v) { return std::isfinite(static_cast<double>(v)); }

// [0, 1]; NaN gives 0
float clamp01(float v) {
    if (!(v > 0)) {
        return 0;
    }
    return v < 1 ? v : 1;
}

std::uint8_t toByte(float v) { return static_cast<std::uint8_t>(clamp01(v) * 255.0f + 0.5f); }

// float -> int for texel coordinates, safe for huge and NaN values
int toIndex(float v) {
    if (!(v > -1.0e6f)) {
        return -1000000;
    }
    return static_cast<int>(std::min(v, 1.0e6f));
}

void transform(const float* m, float x, float y, float& ox, float& oy) {
    ox = m[0] * x + m[2] * y + m[4];
    oy = m[1] * x + m[3] * y + m[5];
}

float scissorMask(const Paint& p, float x, float y) {
    float sx = 0;
    float sy = 0;
    transform(p.scissorMat, x, y, sx, sy);
    sx = 0.5f - (absf(sx) - p.scissorExt[0]) * p.scissorScale[0];
    sy = 0.5f - (absf(sy) - p.scissorExt[1]) * p.scissorScale[1];
    return clamp01(sx) * clamp01(sy);
}

float sdRoundRect(float px, float py, float ex, float ey, float radius) {
    const float dx = absf(px) - (ex - radius);
    const float dy = absf(py) - (ey - radius);
    const float ox = std::max(dx, 0.0f);
    const float oy = std::max(dy, 0.0f);
    return std::min(std::max(dx, dy), 0.0f) + sqrtFloat(ox * ox + oy * oy) - radius;
}

int wrap(int i, int n, bool repeat) {
    if (repeat) {
        i %= n;
        return i < 0 ? i + n : i;
    }
    return std::clamp(i, 0, n - 1);
}

Color texel(const Texture& t, int x, int y) {
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + static_cast<std::size_t>(x);
    if (t.alphaOnly) {
        const float l = static_cast<float>(t.texels[index]) / 255.0f;
        return {l, l, l, 1};
    }
    const std::uint8_t* p = &t.texels[index * 4];
    return {static_cast<float>(p[0]) / 255.0f, static_cast<float>(p[1]) / 255.0f, static_cast<float>(p[2]) / 255.0f,
            static_cast<float>(p[3]) / 255.0f};
}

Color mix(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// GL texture2D: clamp to edge (or repeat), linear (or nearest) filtering, normalised coordinates
Color sample(const Texture& t, float s, float tc) {
    const std::size_t needed = static_cast<std::size_t>(std::max(t.width, 0)) *
                               static_cast<std::size_t>(std::max(t.height, 0)) * (t.alphaOnly ? 1u : 4u);
    if (t.width <= 0 || t.height <= 0 || t.texels.size() < needed) {
        return {0, 0, 0, 0};
    }
    const auto w = static_cast<float>(t.width);
    const auto h = static_cast<float>(t.height);
    if (t.nearest) {
        return texel(t, wrap(toIndex(floorFloat(s * w)), t.width, t.repeatX),
                     wrap(toIndex(floorFloat(tc * h)), t.height, t.repeatY));
    }
    const float fx = s * w - 0.5f;
    const float fy = tc * h - 0.5f;
    const float x0 = floorFloat(fx);
    const float y0 = floorFloat(fy);
    const float ax = finite(fx) ? fx - x0 : 0.0f;
    const float ay = finite(fy) ? fy - y0 : 0.0f;
    const int xa = wrap(toIndex(x0), t.width, t.repeatX);
    const int xb = wrap(toIndex(x0) + 1, t.width, t.repeatX);
    const int ya = wrap(toIndex(y0), t.height, t.repeatY);
    const int yb = wrap(toIndex(y0) + 1, t.height, t.repeatY);
    return mix(mix(texel(t, xa, ya), texel(t, xb, ya), ax), mix(texel(t, xa, yb), texel(t, xb, yb), ax), ay);
}

Color convert(const Color& c, TexType type) {
    switch (type) {
    case TexType::Straight:
        return {c.r * c.a, c.g * c.a, c.b * c.a, c.a};
    case TexType::Alpha:
        return {c.r, c.r, c.r, c.r};
    case TexType::Premultiplied:
        break;
    }
    return c;
}

// NanoVG's fragment shader; false when the fragment is discarded
bool shade(const Paint& p, bool edgeAA, float x, float y, float u, float v, Color& out) {
    const float scissor = scissorMask(p, x, y);
    float strokeAlpha = 1;
    if (edgeAA) {
        strokeAlpha = std::min(1.0f, (1.0f - absf(u * 2.0f - 1.0f)) * p.strokeMult) * std::min(1.0f, v);
        if (strokeAlpha < p.strokeThr) {
            return false;
        }
    }
    switch (p.type) {
    case PaintType::Gradient: {
        float px = 0;
        float py = 0;
        transform(p.paintMat, x, y, px, py);
        const float feather = std::max(p.feather, 1.0e-6f);
        const float d = clamp01((sdRoundRect(px, py, p.extent[0], p.extent[1], p.radius) + feather * 0.5f) / feather);
        const Color inner{p.innerCol[0], p.innerCol[1], p.innerCol[2], p.innerCol[3]};
        const Color outer{p.outerCol[0], p.outerCol[1], p.outerCol[2], p.outerCol[3]};
        const Color c = mix(inner, outer, d);
        const float k = strokeAlpha * scissor;
        out = {c.r * k, c.g * k, c.b * k, c.a * k};
        return true;
    }
    case PaintType::Image: {
        float px = 0;
        float py = 0;
        transform(p.paintMat, x, y, px, py);
        const float ex = std::max(p.extent[0], 1.0e-6f);
        const float ey = std::max(p.extent[1], 1.0e-6f);
        const Color c = convert(p.texture != nullptr ? sample(*p.texture, px / ex, py / ey) : Color{0, 0, 0, 0},
                                p.texType);
        const float k = strokeAlpha * scissor;
        out = {c.r * p.innerCol[0] * k, c.g * p.innerCol[1] * k, c.b * p.innerCol[2] * k, c.a * p.innerCol[3] * k};
        return true;
    }
    case PaintType::StencilOnly:
        out = {1, 1, 1, 1};
        return true;
    case PaintType::TexturedTriangles: {
        const Color c =
            convert(p.texture != nullptr ? sample(*p.texture, u, v) : Color{0, 0, 0, 0}, p.texType);
        out = {c.r * scissor * p.innerCol[0], c.g * scissor * p.innerCol[1], c.b * scissor * p.innerCol[2],
               c.a * scissor * p.innerCol[3]};
        return true;
    }
    }
    return true;
}

bool stencilPasses(StencilTest test, std::uint8_t value) {
    switch (test) {
    case StencilTest::Always:
        return true;
    case StencilTest::EqualZero:
        return value == 0;
    case StencilTest::NotEqualZero:
        return value != 0;
    }
    return true;
}

std::uint8_t stencilApply(StencilOp op, std::uint8_t value) {
    switch (op) {
    case StencilOp::Keep:
        return value;
    case StencilOp::Incr:
        return value == 255 ? value : static_cast<std::uint8_t>(value + 1);
    case StencilOp::IncrWrap:
        return static_cast<std::uint8_t>(value + 1);
    case StencilOp::DecrWrap:
        return static_cast<std::uint8_t>(value - 1);
    case StencilOp::Zero:
        return 0;
    }
    return value;
}

// premultiplied source-over (GL_ONE, GL_ONE_MINUS_SRC_ALPHA on colour and alpha)
void blend(std::uint8_t* d, const Color& c) {
    const float keep = 1.0f - clamp01(c.a);
    d[0] = toByte(clamp01(c.r) + static_cast<float>(d[0]) / 255.0f * keep);
    d[1] = toByte(clamp01(c.g) + static_cast<float>(d[1]) / 255.0f * keep);
    d[2] = toByte(clamp01(c.b) + static_cast<float>(d[2]) / 255.0f * keep);
    d[3] = toByte(clamp01(c.a) + static_cast<float>(d[3]) / 255.0f * keep);
}

// E(P) = (q - p) x (P - p), positive inside once the triangle is oriented with a positive area
struct Edge {
    double a;
    double b;
    double c;
    bool ownsZero;  // top-left rule: pixel centres exactly on the edge belong to top and left edges
};

Edge makeEdge(const Vertex& p, const Vertex& q) {
    const double dx = static_cast<double>(q.x) - static_cast<double>(p.x);
    const double dy = static_cast<double>(q.y) - static_cast<double>(p.y);
    return {-dy, dx, dy * static_cast<double>(p.x) - dx * static_cast<double>(p.y), dy < 0 || (dy == 0 && dx > 0)};
}

bool inside(double value, const Edge& edge) { return value > 0 || (value == 0 && edge.ownsZero); }

int clampCoordinate(double v, int limit) {
    if (!(v > 0)) {
        return 0;
    }
    return v < static_cast<double>(limit) ? static_cast<int>(v) : limit;
}

bool gFastPaths = true;

// counted per draw call in plain integers, then added to the shared atomics once (no cache line ping-pong between
// the band threads on every row)
struct Tally {
    long triangles = 0;
    long nonFinite = 0;
    long degenerate = 0;
    long culled = 0;
    long offRows = 0;
    long rows = 0;
    long pixels = 0;
    long shaded = 0;
    long shadedText = 0;
    long shadedImage = 0;
    long imaged = 0;
    long copied = 0;
};

struct Counters {
    std::atomic<long> triangles{0};
    std::atomic<long> nonFinite{0};
    std::atomic<long> degenerate{0};
    std::atomic<long> culled{0};
    std::atomic<long> offRows{0};
    std::atomic<long> rows{0};
    std::atomic<long> pixels{0};
    std::atomic<long> shaded{0};
    std::atomic<long> shadedText{0};
    std::atomic<long> shadedImage{0};
    std::atomic<long> imaged{0};
    std::atomic<long> copied{0};

    void add(const Tally& t) {
        triangles.fetch_add(t.triangles, std::memory_order_relaxed);
        nonFinite.fetch_add(t.nonFinite, std::memory_order_relaxed);
        degenerate.fetch_add(t.degenerate, std::memory_order_relaxed);
        culled.fetch_add(t.culled, std::memory_order_relaxed);
        offRows.fetch_add(t.offRows, std::memory_order_relaxed);
        rows.fetch_add(t.rows, std::memory_order_relaxed);
        pixels.fetch_add(t.pixels, std::memory_order_relaxed);
        shaded.fetch_add(t.shaded, std::memory_order_relaxed);
        shadedText.fetch_add(t.shadedText, std::memory_order_relaxed);
        shadedImage.fetch_add(t.shadedImage, std::memory_order_relaxed);
        imaged.fetch_add(t.imaged, std::memory_order_relaxed);
        copied.fetch_add(t.copied, std::memory_order_relaxed);
    }
};
Counters gCounters;

// premultiplied source-over of one constant colour, per destination byte: the same arithmetic as blend()
struct BlendTable {
    Color color{-1, -1, -1, -1};
    std::uint8_t channel[4][256];
};

// `table` is kept per band (thread): fans repeat one colour over many triangles
const BlendTable& blendTable(BlendTable& table, const Color& c) {
    if (table.color.r != c.r || table.color.g != c.g || table.color.b != c.b || table.color.a != c.a) {
        table.color = c;
        const float keep = 1.0f - clamp01(c.a);
        const float source[4] = {clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a)};
        for (int k = 0; k < 4; ++k) {
            for (int d = 0; d < 256; ++d) {
                table.channel[k][d] = toByte(source[k] + static_cast<float>(d) / 255.0f * keep);
            }
        }
    }
    return table;
}

// How the scissor covers a triangle: entirely inside (mask 1 everywhere), or an axis-aligned rectangle whose mask is
// tested per row and per pixel (fast paths then only take the pixels at mask 1; the others go through shade()).
enum class Cover { None, Full, Axis };

Cover scissorCover(const Paint& p, const Vertex* v, int n) {
    // the scissor's fully covered area is a convex rectangle: if the corners are in it, so is the whole shape
    bool full = true;
    for (int i = 0; i < n && full; ++i) {
        full = scissorMask(p, v[i].x, v[i].y) >= 1.0f;
    }
    if (full) {
        return Cover::Full;
    }
    return p.scissorMat[1] == 0 && p.scissorMat[2] == 0 ? Cover::Axis : Cover::None;
}

bool sameUV(const Vertex* v, int n) {
    for (int i = 1; i < n; ++i) {
        if (v[i].u != v[0].u || v[i].v != v[0].v) {
            return false;
        }
    }
    return true;
}

// The two factors of scissorMask() for an axis-aligned scissor (same arithmetic: the zero terms add exact zeros);
// the mask is 1 only where both are 1
float scissorFactorX(const Paint& p, float x) {
    const float sx = p.scissorMat[0] * x + p.scissorMat[4];
    return clamp01(0.5f - (absf(sx) - p.scissorExt[0]) * p.scissorScale[0]);
}
float scissorFactorY(const Paint& p, float y) {
    const float sy = p.scissorMat[3] * y + p.scissorMat[5];
    return clamp01(0.5f - (absf(sy) - p.scissorExt[1]) * p.scissorScale[1]);
}

// The colour does not depend on the pixel: a solid gradient (inner = outer) with the same u/v at every vertex (fill
// interiors, the most common triangles). Only the scissor varies.
bool constantColour(const Paint& p, bool edgeAA, const Vertex* v, int n) {
    if (!gFastPaths || p.type != PaintType::Gradient) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (p.innerCol[i] != p.outerCol[i]) {
            return false;
        }
    }
    return !edgeAA || sameUV(v, n);
}

// An unrotated image paint with one edge alpha: sampled row by row (the vertical part is the same for the whole row)
bool imagePath(const Paint& p, bool edgeAA, const Vertex* v, int n) {
    if (!gFastPaths || p.type != PaintType::Image || p.texture == nullptr || p.paintMat[1] != 0 ||
        p.paintMat[2] != 0) {
        return false;
    }
    const Texture& t = *p.texture;
    const std::size_t needed = static_cast<std::size_t>(std::max(t.width, 0)) *
                               static_cast<std::size_t>(std::max(t.height, 0)) * (t.alphaOnly ? 1u : 4u);
    if (t.width <= 0 || t.height <= 0 || t.texels.size() < needed) {
        return false;
    }
    return !edgeAA || sameUV(v, n);
}

struct RowSample {
    int nearestY = 0;
    int ya = 0;
    int yb = 0;
    float ay = 0;
};

RowSample rowSample(const Texture& t, float tc) {
    RowSample r;
    const auto h = static_cast<float>(t.height);
    if (t.nearest) {
        r.nearestY = wrap(toIndex(floorFloat(tc * h)), t.height, t.repeatY);
        return r;
    }
    const float fy = tc * h - 0.5f;
    const float y0 = floorFloat(fy);
    r.ay = finite(fy) ? fy - y0 : 0.0f;
    r.ya = wrap(toIndex(y0), t.height, t.repeatY);
    r.yb = wrap(toIndex(y0) + 1, t.height, t.repeatY);
    return r;
}

Color sampleInRow(const Texture& t, const RowSample& r, float s) {
    const auto w = static_cast<float>(t.width);
    if (t.nearest) {
        return texel(t, wrap(toIndex(floorFloat(s * w)), t.width, t.repeatX), r.nearestY);
    }
    const float fx = s * w - 0.5f;
    const float x0 = floorFloat(fx);
    const float ax = finite(fx) ? fx - x0 : 0.0f;
    const int xa = wrap(toIndex(x0), t.width, t.repeatX);
    const int xb = wrap(toIndex(x0) + 1, t.width, t.repeatX);
    return mix(mix(texel(t, xa, r.ya), texel(t, xb, r.ya), ax), mix(texel(t, xa, r.yb), texel(t, xb, r.yb), ax), r.ay);
}

// Integer bilinear filtering of RGBA images: texel indices from the float arithmetic of sample(), weights
// rounded to 1/256 (the precision of GPU filtering). Results are within 1-2 per channel of the float path.
struct ColumnSample {
    int xa;
    int xb;
    int wx;  // weight of xb, 0..256
};

int weight256(float fraction) { return std::clamp(static_cast<int>(fraction * 256.0f + 0.5f), 0, 256); }

constexpr std::uint32_t kLanes = 0x00FF00FFu;  // two 16-bit lanes: R and B, or G and A, of an RGBA pixel
constexpr std::uint32_t kHalf = 0x00800080u;   // rounding in both lanes

std::uint32_t load32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);  // little-endian: R in the low byte
    return v;
}

// a * (256 - w) + b * w, in 1/256, both lanes
std::uint32_t lerpLanes(std::uint32_t a, std::uint32_t b, std::uint32_t w) {
    return ((a * (256 - w) + b * w + kHalf) >> 8) & kLanes;
}

// each lane (<= 65025) divided by 255, rounded
std::uint32_t div255Lanes(std::uint32_t v) { return ((v + kHalf + ((v >> 8) & kLanes)) >> 8) & kLanes; }

// One constant colour over `count` pixels: the blend table, or a plain store when it is opaque.
void constantSpan(std::uint8_t* d, int count, const BlendTable& table, bool opaque, const std::uint8_t* solid) {
    if (opaque) {
        for (int i = 0; i < count; ++i, d += 4) {
            std::memcpy(d, solid, 4);
        }
        return;
    }
    for (int i = 0; i < count; ++i, d += 4) {  // one load and one store per pixel (byte stores stall on Jaguar)
        const std::uint32_t v = load32(d);
        const std::uint32_t out = static_cast<std::uint32_t>(table.channel[0][v & 0xFFu]) |
                                  static_cast<std::uint32_t>(table.channel[1][(v >> 8) & 0xFFu]) << 8 |
                                  static_cast<std::uint32_t>(table.channel[2][(v >> 16) & 0xFFu]) << 16 |
                                  static_cast<std::uint32_t>(table.channel[3][v >> 24]) << 24;
        std::memcpy(d, &out, 4);
    }
}

struct ImageRow {
    const std::uint8_t* row0;  // texel rows above and below the sample
    const std::uint8_t* row1;
    std::uint32_t wy;          // weight of row1, 0..256
    const int* factor;         // paint colour x edge alpha per channel, 0..256
    TexType type;
    bool opaque;               // opaque texture, untinted: the filtered texel is the pixel
};

// Integer bilinear image pixels (two channels per 32-bit operation), premultiplied, tinted, then blended.
void imageSpan(std::uint8_t* d, int count, const ColumnSample* columns, const ImageRow& r) {
    if (r.opaque) {  // alpha stays 255 through filtering; premultiplying by 255 and blending change nothing
        for (int i = 0; i < count; ++i, d += 4) {
            const ColumnSample& c = columns[i];
            const std::uint32_t p00 = load32(r.row0 + c.xa * 4);
            const std::uint32_t p01 = load32(r.row0 + c.xb * 4);
            const std::uint32_t p10 = load32(r.row1 + c.xa * 4);
            const std::uint32_t p11 = load32(r.row1 + c.xb * 4);
            const auto wx = static_cast<std::uint32_t>(c.wx);
            const std::uint32_t rb = lerpLanes(lerpLanes(p00 & kLanes, p01 & kLanes, wx),
                                               lerpLanes(p10 & kLanes, p11 & kLanes, wx), r.wy);
            const std::uint32_t ga = lerpLanes(lerpLanes((p00 >> 8) & kLanes, (p01 >> 8) & kLanes, wx),
                                               lerpLanes((p10 >> 8) & kLanes, (p11 >> 8) & kLanes, wx), r.wy);
            const std::uint32_t out = rb | (ga << 8);
            std::memcpy(d, &out, 4);
        }
        return;
    }
    const bool uniform = r.factor[0] == r.factor[1] && r.factor[1] == r.factor[2] && r.factor[2] == r.factor[3];
    const auto f = static_cast<std::uint32_t>(r.factor[0]);
    for (int i = 0; i < count; ++i, d += 4) {
        const ColumnSample& c = columns[i];
        const std::uint32_t p00 = load32(r.row0 + c.xa * 4);
        const std::uint32_t p01 = load32(r.row0 + c.xb * 4);
        const std::uint32_t p10 = load32(r.row1 + c.xa * 4);
        const std::uint32_t p11 = load32(r.row1 + c.xb * 4);
        const auto wx = static_cast<std::uint32_t>(c.wx);
        std::uint32_t rb = lerpLanes(lerpLanes(p00 & kLanes, p01 & kLanes, wx),
                                     lerpLanes(p10 & kLanes, p11 & kLanes, wx), r.wy);
        std::uint32_t ga = lerpLanes(lerpLanes((p00 >> 8) & kLanes, (p01 >> 8) & kLanes, wx),
                                     lerpLanes((p10 >> 8) & kLanes, (p11 >> 8) & kLanes, wx), r.wy);
        if (r.type == TexType::Straight) {
            const std::uint32_t alpha = ga >> 16;
            rb = div255Lanes(rb * alpha);
            ga = div255Lanes((ga & 0xFFu) * alpha) | (alpha << 16);
        } else if (r.type == TexType::Alpha) {
            const std::uint32_t l = rb & 0xFFu;
            rb = ga = l | (l << 16);
        }
        if (uniform) {
            if (f != 256) {
                rb = ((rb * f + kHalf) >> 8) & kLanes;
                ga = ((ga * f + kHalf) >> 8) & kLanes;
            }
        } else {
            const auto scale = [](std::uint32_t v, int k) { return (v * static_cast<std::uint32_t>(k) + 128) >> 8; };
            rb = scale(rb & 0xFFu, r.factor[0]) | (scale(rb >> 16, r.factor[2]) << 16);
            ga = scale(ga & 0xFFu, r.factor[1]) | (scale(ga >> 16, r.factor[3]) << 16);
        }
        const std::uint32_t alpha = ga >> 16;
        if (alpha < 255) {
            const std::uint32_t dest = load32(d);
            const std::uint32_t keep = 255 - alpha;
            rb += div255Lanes((dest & kLanes) * keep);
            ga += div255Lanes(((dest >> 8) & kLanes) * keep);
            if (((rb | ga) & 0xFF00FF00u) != 0) {  // a colour above its alpha (premultiplied input): clamp
                rb = std::min(rb & 0xFFFFu, 255u) | (std::min(rb >> 16, 255u) << 16);
                ga = std::min(ga & 0xFFFFu, 255u) | (std::min(ga >> 16, 255u) << 16);
            }
        }
        const std::uint32_t out = rb | (ga << 8);
        std::memcpy(d, &out, 4);
    }
}

// Glyph coverage (alpha atlas, integer bilinear) tinted by the paint colour, then blended.
void textSpan(std::uint8_t* d, int count, const ColumnSample* columns, const ImageRow& r) {
    const auto f = [&r](int k) { return static_cast<std::uint32_t>(r.factor[k]); };
    for (int i = 0; i < count; ++i, d += 4) {
        const ColumnSample& c = columns[i];
        const std::uint32_t top = r.row0[c.xa] * (256u - static_cast<std::uint32_t>(c.wx)) +
                                  r.row0[c.xb] * static_cast<std::uint32_t>(c.wx);
        const std::uint32_t bottom = r.row1[c.xa] * (256u - static_cast<std::uint32_t>(c.wx)) +
                                     r.row1[c.xb] * static_cast<std::uint32_t>(c.wx);
        const std::uint32_t coverage = (top * (256 - r.wy) + bottom * r.wy + 32768) >> 16;
        if (coverage == 0) {
            continue;
        }
        std::uint32_t rb = ((coverage * f(0) + 128) >> 8) | (((coverage * f(2) + 128) >> 8) << 16);
        std::uint32_t ga = ((coverage * f(1) + 128) >> 8) | (((coverage * f(3) + 128) >> 8) << 16);
        const std::uint32_t alpha = ga >> 16;
        if (alpha < 255) {
            const std::uint32_t dest = load32(d);
            const std::uint32_t keep = 255 - alpha;
            rb += div255Lanes((dest & kLanes) * keep);
            ga += div255Lanes(((dest >> 8) & kLanes) * keep);
            if (((rb | ga) & 0xFF00FF00u) != 0) {
                rb = std::min(rb & 0xFFFFu, 255u) | (std::min(rb >> 16, 255u) << 16);
                ga = std::min(ga & 0xFFFFu, 255u) | (std::min(ga >> 16, 255u) << 16);
            }
        }
        const std::uint32_t out = rb | (ga << 8);
        std::memcpy(d, &out, 4);
    }
}

struct Rows {
    int begin;
    int end;
    BlendTable* cache;
    std::vector<ColumnSample>* columns;
    Tally* tally;
};

// A convex shape ready to fill: edges oriented so that the inside is positive, its bounds, and how u/v vary.
constexpr int kMaxEdges = 128;

struct Shape {
    Edge edges[kMaxEdges];
    int edgeCount = 0;
    const Vertex* corners = nullptr;  // the vertices (fast-path checks; u/v when constant)
    int cornerCount = 0;
    bool front = false;
    int x0 = 0;
    int x1 = 0;
    int y0 = 0;
    int y1 = 0;
    bool barycentric = false;  // triangles: u/v interpolated from a, b, c (edges[0..2] = ab, bc, ca)
    Vertex a{};
    Vertex b{};
    Vertex c{};
    double area = 0;
    // text glyph quads: an axis-aligned rectangle whose u/v go linearly from (s0, t0) to (s1, t1)
    bool textRect = false;
    float rx0 = 0;
    float ry0 = 0;
    float rx1 = 0;
    float ry1 = 0;
    float s0 = 0;
    float t0 = 0;
    float s1 = 0;
    float t1 = 0;
};

void fillShape(Surface& s, const Rows& rows, const DrawState& st, const Paint& p, bool edgeAA, const Shape& shape);

// u/v of a glyph rectangle at a pixel centre
float textU(const Shape& r, float px) { return r.s0 + (px - r.rx0) / (r.rx1 - r.rx0) * (r.s1 - r.s0); }
float textV(const Shape& r, float py) { return r.t0 + (py - r.ry0) / (r.ry1 - r.ry0) * (r.t1 - r.t0); }

void triangle(Surface& s, const Rows& rows, const DrawState& st, const Paint& p, bool edgeAA, Vertex a, Vertex b,
              Vertex c) {
    rows.tally->triangles += 1;
    for (const Vertex* v : {&a, &b, &c}) {
        if (!finite(v->x) || !finite(v->y) || !finite(v->u) || !finite(v->v)) {
            rows.tally->nonFinite += 1;
            return;
        }
    }
    // rows first: with several bands most triangles are outside the band, reject them before any other work
    const double minY = std::min({static_cast<double>(a.y), static_cast<double>(b.y), static_cast<double>(c.y)});
    const double maxY = std::max({static_cast<double>(a.y), static_cast<double>(b.y), static_cast<double>(c.y)});
    const int y0 = std::max(rows.begin, clampCoordinate(std::floor(minY), s.height));
    const int y1 = std::min(rows.end, clampCoordinate(std::ceil(maxY), s.height));
    if (y0 >= y1) {
        rows.tally->offRows += 1;
        return;
    }
    double area = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                  (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
    if (area == 0) {
        rows.tally->degenerate += 1;
        return;
    }
    const bool front = area < 0;
    if (st.cull == Cull::Back && !front) {
        rows.tally->culled += 1;
        return;
    }
    if (area < 0) {
        std::swap(b, c);
        area = -area;
    }
    Shape shape;
    shape.edges[0] = makeEdge(a, b);
    shape.edges[1] = makeEdge(b, c);
    shape.edges[2] = makeEdge(c, a);
    shape.edgeCount = 3;
    const Vertex corners[3] = {a, b, c};
    shape.corners = corners;
    shape.cornerCount = 3;
    shape.front = front;
    const double minX = std::min({static_cast<double>(a.x), static_cast<double>(b.x), static_cast<double>(c.x)});
    const double maxX = std::max({static_cast<double>(a.x), static_cast<double>(b.x), static_cast<double>(c.x)});
    shape.x0 = clampCoordinate(std::floor(minX), s.width);
    shape.x1 = clampCoordinate(std::ceil(maxX), s.width);
    shape.y0 = y0;
    shape.y1 = y1;
    shape.barycentric = true;
    shape.a = a;
    shape.b = b;
    shape.c = c;
    shape.area = area;
    fillShape(s, rows, st, p, edgeAA, shape);
}

// A convex polygon with one u/v (NanoVG's convex fill interiors): the union of its fan triangles, filled with one
// span per row instead of one per triangle and row.
void convexPolygon(Surface& s, const Rows& rows, const DrawState& st, const Paint& p, bool edgeAA, const Vertex* v,
                   int n) {
    rows.tally->triangles += 1;
    double minY = 0;
    double maxY = 0;
    double minX = 0;
    double maxX = 0;
    double twiceArea = 0;
    for (int i = 0; i < n; ++i) {
        if (!finite(v[i].x) || !finite(v[i].y) || !finite(v[i].u) || !finite(v[i].v)) {
            rows.tally->nonFinite += 1;
            return;
        }
        const auto x = static_cast<double>(v[i].x);
        const auto y = static_cast<double>(v[i].y);
        minX = i == 0 ? x : std::min(minX, x);
        maxX = i == 0 ? x : std::max(maxX, x);
        minY = i == 0 ? y : std::min(minY, y);
        maxY = i == 0 ? y : std::max(maxY, y);
        const Vertex& next = v[(i + 1) % n];
        twiceArea += x * static_cast<double>(next.y) - static_cast<double>(next.x) * y;
    }
    const int y0 = std::max(rows.begin, clampCoordinate(std::floor(minY), s.height));
    const int y1 = std::min(rows.end, clampCoordinate(std::ceil(maxY), s.height));
    if (y0 >= y1) {
        rows.tally->offRows += 1;
        return;
    }
    if (twiceArea == 0) {
        rows.tally->degenerate += 1;
        return;
    }
    const bool front = twiceArea < 0;
    if (st.cull == Cull::Back && !front) {
        rows.tally->culled += 1;
        return;
    }
    Shape shape;
    for (int i = 0; i < n; ++i) {
        const Vertex& p0 = v[i];
        const Vertex& p1 = v[(i + 1) % n];
        if (p0.x == p1.x && p0.y == p1.y) {
            continue;  // repeated point: no edge
        }
        shape.edges[shape.edgeCount++] = twiceArea > 0 ? makeEdge(p0, p1) : makeEdge(p1, p0);
    }
    shape.corners = v;
    shape.cornerCount = n;
    shape.front = front;
    shape.x0 = clampCoordinate(std::floor(minX), s.width);
    shape.x1 = clampCoordinate(std::ceil(maxX), s.width);
    shape.y0 = y0;
    shape.y1 = y1;
    fillShape(s, rows, st, p, edgeAA, shape);
}

// NanoVG's glyph quad (nvgText without rotation): (x0,y0) (x1,y1) (x1,y0), (x0,y0) (x0,y1) (x1,y1), u/v following
bool isTextQuad(const Vertex* g) {
    const Vertex& a = g[0];
    const Vertex& b = g[1];
    return g[3].x == a.x && g[3].y == a.y && g[3].u == a.u && g[3].v == a.v && g[5].x == b.x && g[5].y == b.y &&
           g[5].u == b.u && g[5].v == b.v && g[2].x == b.x && g[2].y == a.y && g[2].u == b.u && g[2].v == a.v &&
           g[4].x == a.x && g[4].y == b.y && g[4].u == a.u && g[4].v == b.v && a.x < b.x && a.y < b.y;
}

// A glyph quad as one rectangle (the union of its two triangles), textured through the linear u/v mapping.
void textQuad(Surface& s, const Rows& rows, const DrawState& st, const Paint& p, bool edgeAA, const Vertex* g) {
    rows.tally->triangles += 1;
    const Vertex& a = g[0];
    const Vertex& b = g[1];
    for (const Vertex* v : {&a, &b}) {
        if (!finite(v->x) || !finite(v->y) || !finite(v->u) || !finite(v->v)) {
            rows.tally->nonFinite += 1;
            return;
        }
    }
    const int y0 = std::max(rows.begin, clampCoordinate(std::floor(static_cast<double>(a.y)), s.height));
    const int y1 = std::min(rows.end, clampCoordinate(std::ceil(static_cast<double>(b.y)), s.height));
    if (y0 >= y1) {
        rows.tally->offRows += 1;
        return;
    }
    // corners in the order of the two triangles' outline: (x0,y0) (x1,y0) (x1,y1) (x0,y1); both triangles face
    // the same way (front in screen space), so the outline is oriented like them
    const Vertex corners[4] = {a, g[2], b, g[4]};
    Shape shape;
    for (int i = 0; i < 4; ++i) {
        shape.edges[shape.edgeCount++] = makeEdge(corners[i], corners[(i + 1) % 4]);  // positive area: inside > 0
    }
    shape.corners = corners;
    shape.cornerCount = 4;
    shape.front = true;
    shape.x0 = clampCoordinate(std::floor(static_cast<double>(a.x)), s.width);
    shape.x1 = clampCoordinate(std::ceil(static_cast<double>(b.x)), s.width);
    shape.y0 = y0;
    shape.y1 = y1;
    shape.textRect = true;
    shape.rx0 = a.x;
    shape.ry0 = a.y;
    shape.rx1 = b.x;
    shape.ry1 = b.y;
    shape.s0 = a.u;
    shape.t0 = a.v;
    shape.s1 = b.u;
    shape.t1 = b.v;
    fillShape(s, rows, st, p, edgeAA, shape);
}

void fillShape(Surface& s, const Rows& rows, const DrawState& st, const Paint& p, bool edgeAA, const Shape& shape) {
    const int x0 = shape.x0;
    const int x1 = shape.x1;
    const int y0 = shape.y0;
    const int y1 = shape.y1;
    const bool front = shape.front;
    const Vertex* corners = shape.corners;
    const int cornerCount = shape.cornerCount;
    const StencilOp passOp = front ? st.passFront : st.passBack;
    const bool stencilIdle = st.test == StencilTest::Always && passOp == StencilOp::Keep;
    const bool needShade = st.writeColor || p.strokeThr > -1.0f;

    // Fast paths: the pixels where the scissor mask is 1 are drawn without shade(); the others (scissor edges)
    // take the general path, and those at mask 0 are skipped when nothing but the colour would change.
    enum class Fast { None, Constant, Image, Text };
    Fast fast = Fast::None;
    Cover cover = Cover::None;
    Color constant{1, 1, 1, 1};
    float imageAlpha = 1;  // edge alpha, constant over the shape
    if (needShade && constantColour(p, edgeAA, corners, cornerCount)) {
        cover = scissorCover(p, corners, cornerCount);
        if (cover != Cover::None) {
            Paint unclipped = p;  // the colour where the mask is 1
            std::fill(std::begin(unclipped.scissorMat), std::end(unclipped.scissorMat), 0.0f);
            unclipped.scissorExt[0] = unclipped.scissorExt[1] = 1;
            unclipped.scissorScale[0] = unclipped.scissorScale[1] = 1;
            if (!shade(unclipped, edgeAA, corners[0].x, corners[0].y, corners[0].u, corners[0].v, constant)) {
                return;  // discarded everywhere (edge alpha below the threshold): no colour, no stencil change
            }
            fast = Fast::Constant;
        }
    } else if (needShade && st.writeColor && imagePath(p, edgeAA, corners, cornerCount)) {
        cover = scissorCover(p, corners, cornerCount);
        if (cover != Cover::None) {
            if (edgeAA) {
                imageAlpha = std::min(1.0f, (1.0f - absf(corners[0].u * 2.0f - 1.0f)) * p.strokeMult) *
                             std::min(1.0f, corners[0].v);
                if (imageAlpha < p.strokeThr) {
                    return;  // discarded everywhere
                }
            }
            fast = Fast::Image;
        }
    } else if (gFastPaths && shape.textRect && st.writeColor && p.type == PaintType::TexturedTriangles &&
               p.texture != nullptr && p.texture->alphaOnly && p.strokeThr <= -1.0f &&
               p.texType == TexType::Alpha && p.texture->width > 0 && p.texture->height > 0 &&
               p.texture->texels.size() >= static_cast<std::size_t>(p.texture->width) *
                                               static_cast<std::size_t>(p.texture->height)) {
        cover = scissorCover(p, corners, cornerCount);
        if (cover != Cover::None) {
            fast = Fast::Text;  // the edge alpha is never below the threshold: nothing is discarded
        }
    }
    const BlendTable* table =
        (fast == Fast::Constant && st.writeColor) ? &blendTable(*rows.cache, constant) : nullptr;
    const bool opaque = table != nullptr && clamp01(constant.a) >= 1.0f;  // the table maps every byte to one value
    std::uint8_t solid[4] = {0, 0, 0, 0};
    if (opaque) {
        for (int k = 0; k < 4; ++k) {
            solid[k] = table->channel[k][0];
        }
    }

    // integer image sampling: RGBA textures; column samples computed once for the triangle
    const Texture* texture = fast == Fast::Image || fast == Fast::Text ? p.texture : nullptr;
    const bool integerImage = fast == Fast::Image && !texture->alphaOnly;
    const bool text = fast == Fast::Text;
    int factor[4] = {256, 256, 256, 256};
    const float ex = std::max(p.extent[0], 1.0e-6f);
    const float ey = std::max(p.extent[1], 1.0e-6f);
    bool identityColumns = true;  // pixel x shows texel xa(x0) + (x - x0) exactly: rows are plain copies
    if (integerImage || text) {
        std::vector<ColumnSample>& columns = *rows.columns;
        columns.resize(static_cast<std::size_t>(std::max(x1 - x0, 0)));
        const auto w = static_cast<float>(texture->width);
        for (int x = x0; x < x1; ++x) {
            float sCoord = 0;
            if (text) {
                sCoord = textU(shape, static_cast<float>(x + 0.5));
            } else {
                float tx = 0;
                float unused = 0;
                transform(p.paintMat, static_cast<float>(x + 0.5), 0.0f, tx, unused);
                sCoord = tx / ex;
            }
            ColumnSample& col = columns[static_cast<std::size_t>(x - x0)];
            if (texture->nearest) {
                col.xa = col.xb = wrap(toIndex(floorFloat(sCoord * w)), texture->width, texture->repeatX);
                col.wx = 0;
            } else {
                const float fx = sCoord * w - 0.5f;
                const float xf = floorFloat(fx);
                col.xa = wrap(toIndex(xf), texture->width, texture->repeatX);
                col.xb = wrap(toIndex(xf) + 1, texture->width, texture->repeatX);
                col.wx = weight256(finite(fx) ? fx - xf : 0.0f);
                if (col.wx == 256) {  // all of the next texel (a centre hit from just below)
                    col.xa = col.xb;
                    col.wx = 0;
                }
            }
            identityColumns = identityColumns && col.wx == 0 &&
                              (x == x0 || col.xa == columns[static_cast<std::size_t>(x - x0 - 1)].xa + 1);
        }
        for (int k = 0; k < 4; ++k) {
            factor[k] = weight256(clamp01(p.innerCol[k] * (text ? 1.0f : imageAlpha)));
        }
    }
    const bool opaqueImage = integerImage && texture->opaque && p.texType != TexType::Alpha && factor[0] == 256 &&
                             factor[1] == 256 && factor[2] == 256 && factor[3] == 256;

    for (int y = y0; y < y1; ++y) {
        const double py = y + 0.5;
        // the inside test, exactly as the rule states it (the same expressions as the general path)
        auto insideAt = [&](int x) {
            const double px = x + 0.5;
            for (int k = 0; k < shape.edgeCount; ++k) {
                const Edge& e = shape.edges[k];
                if (!inside(e.a * px + e.b * py + e.c, e)) {
                    return false;
                }
            }
            return true;
        };
        // span from the edge equations, then corrected so that it is exactly the set insideAt accepts
        double lo = x0;
        double hi = x1;
        bool empty = false;
        for (int k = 0; k < shape.edgeCount; ++k) {
            const Edge& e = shape.edges[k];
            const double rest = e.b * py + e.c;
            if (e.a > 0) {
                lo = std::max(lo, std::floor(-rest / e.a - 0.5));
            } else if (e.a < 0) {
                hi = std::min(hi, std::ceil(-rest / e.a - 0.5) + 1.0);
            } else if (!inside(rest, e)) {
                empty = true;
            }
        }
        if (empty || !(lo < hi)) {
            continue;
        }
        int xs = std::max(x0, std::min(x1, static_cast<int>(lo)));
        int xe = std::max(xs, std::min(x1, static_cast<int>(hi)));
        while (xs < xe && !insideAt(xs)) {
            ++xs;
        }
        while (xs > x0 && insideAt(xs - 1)) {
            --xs;
        }
        while (xe > xs && !insideAt(xe - 1)) {
            --xe;
        }
        while (xe < x1 && xe >= xs && insideAt(xe)) {
            ++xe;
        }
        if (xs >= xe) {
            continue;
        }
        rows.tally->rows += 1;
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width);
        long written = 0;
        long fastWritten = 0;
        // the row's scissor factor: below 1 every pixel of the row needs the general path (or nothing at 0)
        float rowFactor = 1;
        if (cover == Cover::Axis) {
            rowFactor = scissorFactorY(p, static_cast<float>(py));
        }
        RowSample sampleRow;
        ImageRow imageRow{nullptr, nullptr, 0, factor, p.texType, opaqueImage};
        if (fast == Fast::Image || text) {
            float unused = 0;
            float ty = 0;
            if (!text) {
                transform(p.paintMat, static_cast<float>(xs) + 0.5f, static_cast<float>(py), unused, ty);
            }
            sampleRow = rowSample(*texture, text ? textV(shape, static_cast<float>(py)) : ty / ey);
            if (integerImage || text) {
                const std::size_t stride = static_cast<std::size_t>(texture->width) * (text ? 1u : 4u);
                const int ya = texture->nearest ? sampleRow.nearestY : sampleRow.ya;
                const int yb = texture->nearest ? sampleRow.nearestY : sampleRow.yb;
                imageRow.row0 = texture->texels.data() + static_cast<std::size_t>(ya) * stride;
                imageRow.row1 = texture->texels.data() + static_cast<std::size_t>(yb) * stride;
                imageRow.wy = texture->nearest ? 0u : static_cast<std::uint32_t>(weight256(sampleRow.ay));
                if (imageRow.wy == 256) {
                    imageRow.row0 = imageRow.row1;
                    imageRow.wy = 0;
                }
            }
        }
        const auto pixelAt = [&](int x) {
            const std::size_t index = row + static_cast<std::size_t>(x);
            // which path draws this pixel
            bool fastPixel = fast != Fast::None;
            if (fastPixel && cover == Cover::Axis) {
                const float mask = rowFactor * scissorFactorX(p, static_cast<float>(x + 0.5));
                if (mask <= 0 && stencilIdle && st.writeColor) {
                    return;  // shade() would give a zero colour: the blend leaves the pixel as it is
                }
                fastPixel = mask >= 1;
            }
            fastPixel = fastPixel && !text;  // single text pixels (scissor edges) take the general path
            Color color = constant;
            if (fastPixel && fast == Fast::Image && !integerImage) {
                float tx = 0;
                float unused = 0;
                transform(p.paintMat, static_cast<float>(x + 0.5), static_cast<float>(py), tx, unused);
                const Color t = convert(sampleInRow(*texture, sampleRow, tx / ex), p.texType);
                color = {t.r * p.innerCol[0] * imageAlpha, t.g * p.innerCol[1] * imageAlpha,
                         t.b * p.innerCol[2] * imageAlpha, t.a * p.innerCol[3] * imageAlpha};
            } else if (!fastPixel && needShade) {
                const double px = x + 0.5;
                float u = corners[0].u;
                float v = corners[0].v;
                if (shape.textRect) {
                    u = textU(shape, static_cast<float>(px));
                    v = textV(shape, static_cast<float>(py));
                } else if (shape.barycentric) {
                    const Edge& ab = shape.edges[0];
                    const Edge& bc = shape.edges[1];
                    const Edge& ca = shape.edges[2];
                    const double wa = (bc.a * px + bc.b * py + bc.c) / shape.area;
                    const double wb = (ca.a * px + ca.b * py + ca.c) / shape.area;
                    const double wc = (ab.a * px + ab.b * py + ab.c) / shape.area;
                    u = static_cast<float>(wa * shape.a.u + wb * shape.b.u + wc * shape.c.u);
                    v = static_cast<float>(wa * shape.a.v + wb * shape.b.v + wc * shape.c.v);
                }
                if (!shade(p, edgeAA, static_cast<float>(px), static_cast<float>(py), u, v, color)) {
                    return;
                }
            }
            if (!stencilIdle) {
                std::uint8_t& stencil = s.stencil[index];
                if (!stencilPasses(st.test, stencil)) {
                    stencil = stencilApply(st.fail, stencil);
                    return;
                }
                stencil = stencilApply(passOp, stencil);
            }
            ++written;
            fastWritten += fastPixel ? 1 : 0;
            if (!st.writeColor) {
                return;
            }
            std::uint8_t* d = &s.rgba[index * 4];
            if (fastPixel && fast == Fast::Constant) {
                constantSpan(d, 1, *table, opaque, solid);
            } else if (fastPixel && integerImage) {
                imageSpan(d, 1, &(*rows.columns)[static_cast<std::size_t>(x - x0)], imageRow);
            } else {
                blend(d, color);
            }
        };
        // the span's interior where the scissor mask is 1, drawn by the tight loops (no stencil, colour written)
        int fs = xe;
        int fe = xe;
        if (fast != Fast::None && stencilIdle && st.writeColor && rowFactor >= 1 &&
            (fast == Fast::Constant || integerImage || text)) {
            fs = xs;
            fe = xe;
            if (cover == Cover::Axis) {  // the mask is 1 on one interval of the row
                while (fs < fe && scissorFactorX(p, static_cast<float>(fs + 0.5)) < 1) {
                    ++fs;
                }
                while (fe > fs && scissorFactorX(p, static_cast<float>(fe - 0.5)) < 1) {
                    --fe;
                }
            }
        }
        for (int x = xs; x < fs; ++x) {
            pixelAt(x);
        }
        if (fs < fe) {
            std::uint8_t* d = &s.rgba[(row + static_cast<std::size_t>(fs)) * 4];
            if (fast == Fast::Constant) {
                constantSpan(d, fe - fs, *table, opaque, solid);
            } else {
                const ColumnSample* columns = &(*rows.columns)[static_cast<std::size_t>(fs - x0)];
                if (text) {
                    textSpan(d, fe - fs, columns, imageRow);
                } else if (opaqueImage && identityColumns && imageRow.wy == 0) {
                    std::memcpy(d, imageRow.row0 + static_cast<std::size_t>(columns->xa) * 4,
                                static_cast<std::size_t>(fe - fs) * 4);  // 1:1 on whole pixels
                    rows.tally->copied += fe - fs;
                } else {
                    imageSpan(d, fe - fs, columns, imageRow);
                }
            }
            written += fe - fs;
            fastWritten += fe - fs;
        }
        for (int x = std::max(fe, fs); x < xe; ++x) {
            pixelAt(x);
        }
        rows.tally->pixels += written;
        if (fast == Fast::Image) {
            rows.tally->imaged += fastWritten;
        }
        if (needShade && written > fastWritten) {
            rows.tally->shaded += written - fastWritten;
            if (p.type == PaintType::TexturedTriangles) {
                rows.tally->shadedText += written - fastWritten;
            } else if (p.type == PaintType::Image) {
                rows.tally->shadedImage += written - fastWritten;
            }
        }
    }
}

}  // namespace

Surface::Surface(int w, int h)
    : width(std::max(w, 0)),
      height(std::max(h, 0)),
      rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4),
      stencil(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {}

void Surface::clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (rgba.empty()) {
        return;
    }
    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = a;
    for (std::size_t filled = 4; filled < rgba.size();) {  // doubling copies of the first pixel
        const std::size_t count = std::min(filled, rgba.size() - filled);
        std::memcpy(rgba.data() + filled, rgba.data(), count);
        filled += count;
    }
    std::fill(stencil.begin(), stencil.end(), 0);
}

struct Band::Cache {
    BlendTable table;
    std::vector<ColumnSample> columns;
};

Band::Band(int rowBegin, int rowEnd) : rowBegin_(rowBegin), rowEnd_(rowEnd), cache_(std::make_unique<Cache>()) {}

Band::~Band() = default;

void Band::draw(Surface& surface, const DrawState& state, const Paint& paint, bool edgeAA, Primitive primitive,
                const Vertex* v, int count) {
    if (v == nullptr || count < 3 || surface.width <= 0 || surface.height <= 0) {
        return;
    }
    Tally tally;
    const Rows rows{std::max(rowBegin_, 0), std::min(rowEnd_, surface.height), &cache_->table, &cache_->columns, &tally};
    if (rows.begin >= rows.end) {
        return;
    }
    switch (primitive) {
    case Primitive::Triangles:
        for (int i = 0; i + 2 < count;) {
            if (gFastPaths && paint.type == PaintType::TexturedTriangles && i + 5 < count && isTextQuad(v + i)) {
                textQuad(surface, rows, state, paint, edgeAA, v + i);
                i += 6;
                continue;
            }
            triangle(surface, rows, state, paint, edgeAA, v[i], v[i + 1], v[i + 2]);
            i += 3;
        }
        break;
    case Primitive::Strip:
        for (int i = 0; i + 2 < count; ++i) {
            if (i % 2 == 0) {
                triangle(surface, rows, state, paint, edgeAA, v[i], v[i + 1], v[i + 2]);
            } else {
                triangle(surface, rows, state, paint, edgeAA, v[i + 1], v[i], v[i + 2]);
            }
        }
        break;
    case Primitive::ConvexPolygon:
        if (count <= kMaxEdges && sameUV(v, count)) {
            convexPolygon(surface, rows, state, paint, edgeAA, v, count);
            break;
        }
        [[fallthrough]];  // u/v vary or too many vertices: its fan
    case Primitive::Fan:
        for (int i = 1; i + 1 < count; ++i) {
            triangle(surface, rows, state, paint, edgeAA, v[0], v[i], v[i + 1]);
        }
        break;
    }
    gCounters.add(tally);
}

void draw(Surface& surface, const DrawState& state, const Paint& paint, bool edgeAA, Primitive primitive,
          const Vertex* v, int count) {
    static Band all(0, 1 << 30);  // single-threaded callers (tests)
    all.draw(surface, state, paint, edgeAA, primitive, v, count);
}

void setFastPathsForTesting(bool enabled) { gFastPaths = enabled; }

RasterStats rasterStats() {
    RasterStats stats;
    stats.triangles = gCounters.triangles.load();
    stats.nonFinite = gCounters.nonFinite.load();
    stats.degenerate = gCounters.degenerate.load();
    stats.culled = gCounters.culled.load();
    stats.offRows = gCounters.offRows.load();
    stats.rows = gCounters.rows.load();
    stats.pixels = gCounters.pixels.load();
    stats.shaded = gCounters.shaded.load();
    stats.shadedText = gCounters.shadedText.load();
    stats.shadedImage = gCounters.shadedImage.load();
    stats.imaged = gCounters.imaged.load();
    stats.copied = gCounters.copied.load();
    return stats;
}

void resetRasterStats() {
    for (std::atomic<long>* counter : {&gCounters.triangles, &gCounters.nonFinite, &gCounters.degenerate,
                                       &gCounters.culled, &gCounters.offRows, &gCounters.rows, &gCounters.pixels,
                                       &gCounters.shaded, &gCounters.shadedText, &gCounters.shadedImage,
                                       &gCounters.imaged, &gCounters.copied}) {
        counter->store(0);
    }
}

}  // namespace playfin::render
