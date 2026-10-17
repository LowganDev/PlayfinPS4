#include "render/nvg_sw.hpp"

#include <nanovg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "core/log.hpp"
#include "render/raster.hpp"

namespace playfin::render {

namespace {

enum class CallType { Fill, ConvexFill, Stroke, Triangles };

struct PathRange {
    int fillOffset = 0;
    int fillCount = 0;
    int strokeOffset = 0;
    int strokeCount = 0;
};

// One recorded NanoVG call (GLNVGcall).
struct Call {
    CallType type = CallType::Triangles;
    int pathOffset = 0;
    int pathCount = 0;
    int triangleOffset = 0;
    int triangleCount = 0;
    Paint paint;   // Fill: the stencil pass. Stroke: the anti-aliased pass. Others: the paint.
    Paint second;  // Fill: fringes and cover. Stroke: the base pass (discards below full alpha).
};

struct TextureEntry {
    Texture texture;
    int flags = 0;
};

// Threads that each replay the frame into their own band; the caller's thread takes band 0.
class Workers {
public:
    explicit Workers(std::size_t threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            threads_.emplace_back([this, i] { loop(i + 1); });
        }
    }
    ~Workers() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        for (std::thread& thread : threads_) {
            thread.join();
        }
    }
    Workers(const Workers&) = delete;
    Workers& operator=(const Workers&) = delete;

    // job(0) here and job(1 .. count - 1) on the threads; returns when all are done
    void run(std::size_t count, const std::function<void(std::size_t)>& job) {
        count = std::min(count, threads_.size() + 1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = &job;
            count_ = count;
            pending_ = count - 1;
            ++generation_;
        }
        wake_.notify_all();
        job(0);
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return pending_ == 0; });
        job_ = nullptr;
    }

private:
    void loop(std::size_t index) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(std::size_t)>* job = nullptr;
            std::size_t count = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this, seen] { return stop_ || generation_ != seen; });
                if (stop_) {
                    return;
                }
                seen = generation_;
                job = job_;
                count = count_;
            }
            if (index < count) {
                (*job)(index);
                std::lock_guard<std::mutex> lock(mutex_);
                if (--pending_ == 0) {
                    done_.notify_one();
                }
            }
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    const std::function<void(std::size_t)>* job_ = nullptr;
    std::size_t count_ = 0;
    std::size_t pending_ = 0;
    std::uint64_t generation_ = 0;
    bool stop_ = false;
};

struct Backend {
    Surface* surface = nullptr;
    std::vector<std::unique_ptr<Band>> bands;
    std::unique_ptr<Workers> workers;
    std::size_t threads = 1;
    std::map<int, TextureEntry> textures;  // stable addresses: paints point at them until the flush
    std::vector<int> deleted;              // freed after the frame that may still draw them
    int nextTexture = 1;
    std::vector<Vertex> vertices;
    std::vector<PathRange> paths;
    std::vector<Call> calls;
};

Backend& backendOf(void* user) { return *static_cast<Backend*>(user); }

void premultiplied(const NVGcolor& c, float* out) {
    out[0] = c.rgba[0] * c.rgba[3];
    out[1] = c.rgba[1] * c.rgba[3];
    out[2] = c.rgba[2] * c.rgba[3];
    out[3] = c.rgba[3];
}

int append(Backend& b, const NVGvertex* vertices, int count) {
    const int offset = static_cast<int>(b.vertices.size());
    for (int i = 0; i < count; ++i) {
        b.vertices.push_back({vertices[i].x, vertices[i].y, vertices[i].u, vertices[i].v});
    }
    return offset;
}

// glnvg__convertPaint
bool convertPaint(Backend& b, Paint& out, const NVGpaint& paint, const NVGscissor& scissor, float width,
                  float fringe, float strokeThr) {
    out = Paint{};
    premultiplied(paint.innerColor, out.innerCol);
    premultiplied(paint.outerColor, out.outerCol);
    if (!(scissor.extent[0] < -0.5f || scissor.extent[1] < -0.5f)) {
        nvgTransformInverse(out.scissorMat, scissor.xform);
        out.scissorExt[0] = scissor.extent[0];
        out.scissorExt[1] = scissor.extent[1];
        out.scissorScale[0] = static_cast<float>(std::sqrt(static_cast<double>(
                                  scissor.xform[0] * scissor.xform[0] + scissor.xform[2] * scissor.xform[2]))) /
                              fringe;
        out.scissorScale[1] = static_cast<float>(std::sqrt(static_cast<double>(
                                  scissor.xform[1] * scissor.xform[1] + scissor.xform[3] * scissor.xform[3]))) /
                              fringe;
    }
    out.extent[0] = paint.extent[0];
    out.extent[1] = paint.extent[1];
    out.strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    out.strokeThr = strokeThr;
    float inverse[6];
    if (paint.image != 0) {
        const auto it = b.textures.find(paint.image);
        if (it == b.textures.end()) {
            return false;
        }
        if ((it->second.flags & NVG_IMAGE_FLIPY) != 0) {
            float m1[6];
            float m2[6];
            nvgTransformTranslate(m1, 0.0f, out.extent[1] * 0.5f);
            nvgTransformMultiply(m1, paint.xform);
            nvgTransformScale(m2, 1.0f, -1.0f);
            nvgTransformMultiply(m2, m1);
            nvgTransformTranslate(m1, 0.0f, -out.extent[1] * 0.5f);
            nvgTransformMultiply(m1, m2);
            nvgTransformInverse(inverse, m1);
        } else {
            nvgTransformInverse(inverse, paint.xform);
        }
        out.type = PaintType::Image;
        out.texture = &it->second.texture;
        if (it->second.texture.alphaOnly) {
            out.texType = TexType::Alpha;
        } else {
            out.texType = (it->second.flags & NVG_IMAGE_PREMULTIPLIED) != 0 ? TexType::Premultiplied : TexType::Straight;
        }
    } else {
        out.type = PaintType::Gradient;
        out.radius = paint.radius;
        out.feather = paint.feather;
        nvgTransformInverse(inverse, paint.xform);
    }
    std::memcpy(out.paintMat, inverse, sizeof(inverse));
    return true;
}

int renderCreate(void*) { return 1; }

// RGBA textures whose alpha is 255 everywhere are drawn without blending
void updateOpaque(Texture& t) {
    t.opaque = !t.alphaOnly;
    for (std::size_t i = 3; i < t.texels.size() && t.opaque; i += 4) {
        t.opaque = t.texels[i] == 255;
    }
}

int renderCreateTexture(void* user, int type, int w, int h, int imageFlags, const unsigned char* data) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        return 0;
    }
    Backend& b = backendOf(user);
    TextureEntry entry;
    entry.flags = imageFlags;
    Texture& t = entry.texture;
    t.width = w;
    t.height = h;
    t.alphaOnly = type == NVG_TEXTURE_ALPHA;
    t.repeatX = (imageFlags & NVG_IMAGE_REPEATX) != 0;
    t.repeatY = (imageFlags & NVG_IMAGE_REPEATY) != 0;
    t.nearest = (imageFlags & NVG_IMAGE_NEAREST) != 0;
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * (t.alphaOnly ? 1u : 4u);
    t.texels.assign(bytes, 0);
    if (data != nullptr) {
        std::memcpy(t.texels.data(), data, bytes);
    }
    updateOpaque(t);
    const int id = b.nextTexture++;
    b.textures.emplace(id, std::move(entry));
    return id;
}

int renderDeleteTexture(void* user, int image) {
    Backend& b = backendOf(user);
    if (b.textures.count(image) == 0) {
        return 0;
    }
    b.deleted.push_back(image);
    return 1;
}

// As the GLES2 backend: `data` is the whole image, and rows y .. y + h are copied in full.
int renderUpdateTexture(void* user, int image, int x, int y, int w, int h, const unsigned char* data) {
    (void)x;
    (void)w;
    Backend& b = backendOf(user);
    const auto it = b.textures.find(image);
    if (it == b.textures.end() || data == nullptr) {
        return 0;
    }
    Texture& t = it->second.texture;
    if (y < 0 || h < 0 || y > t.height - h) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(t.width) * (t.alphaOnly ? 1u : 4u);
    const std::size_t start = static_cast<std::size_t>(y) * row;
    std::memcpy(t.texels.data() + start, data + start, static_cast<std::size_t>(h) * row);
    if (!t.alphaOnly) {
        updateOpaque(t);
    }
    return 1;
}

int renderGetTextureSize(void* user, int image, int* w, int* h) {
    Backend& b = backendOf(user);
    const auto it = b.textures.find(image);
    if (it == b.textures.end()) {
        return 0;
    }
    *w = it->second.texture.width;
    *h = it->second.texture.height;
    return 1;
}

void renderViewport(void*, float, float, float) {}

void renderCancel(void* user) {
    Backend& b = backendOf(user);
    b.vertices.clear();
    b.paths.clear();
    b.calls.clear();
    for (int image : b.deleted) {
        b.textures.erase(image);
    }
    b.deleted.clear();
}

void renderFill(void* user, NVGpaint* paint, NVGcompositeOperationState, NVGscissor* scissor, float fringe,
                const float* bounds, const NVGpath* paths, int npaths) {
    Backend& b = backendOf(user);
    Call call;
    call.type = (npaths == 1 && paths[0].convex != 0) ? CallType::ConvexFill : CallType::Fill;
    call.pathOffset = static_cast<int>(b.paths.size());
    call.pathCount = npaths;
    for (int i = 0; i < npaths; ++i) {
        PathRange range;
        if (paths[i].nfill > 0) {
            range.fillOffset = append(b, paths[i].fill, paths[i].nfill);
            range.fillCount = paths[i].nfill;
        }
        if (paths[i].nstroke > 0) {
            range.strokeOffset = append(b, paths[i].stroke, paths[i].nstroke);
            range.strokeCount = paths[i].nstroke;
        }
        b.paths.push_back(range);
    }
    if (call.type == CallType::Fill) {
        const NVGvertex quad[4] = {{bounds[2], bounds[3], 0.5f, 1.0f},
                                   {bounds[2], bounds[1], 0.5f, 1.0f},
                                   {bounds[0], bounds[3], 0.5f, 1.0f},
                                   {bounds[0], bounds[1], 0.5f, 1.0f}};
        call.triangleOffset = append(b, quad, 4);
        call.triangleCount = 4;
        call.paint = Paint{};  // the stencil pass: no colour
        if (!convertPaint(b, call.second, *paint, *scissor, fringe, fringe, -1.0f)) {
            return;
        }
    } else if (!convertPaint(b, call.paint, *paint, *scissor, fringe, fringe, -1.0f)) {
        return;
    }
    b.calls.push_back(call);
}

void renderStroke(void* user, NVGpaint* paint, NVGcompositeOperationState, NVGscissor* scissor, float fringe,
                  float strokeWidth, const NVGpath* paths, int npaths) {
    Backend& b = backendOf(user);
    Call call;
    call.type = CallType::Stroke;
    call.pathOffset = static_cast<int>(b.paths.size());
    call.pathCount = npaths;
    for (int i = 0; i < npaths; ++i) {
        PathRange range;
        if (paths[i].nstroke > 0) {
            range.strokeOffset = append(b, paths[i].stroke, paths[i].nstroke);
            range.strokeCount = paths[i].nstroke;
        }
        b.paths.push_back(range);
    }
    if (!convertPaint(b, call.paint, *paint, *scissor, strokeWidth, fringe, -1.0f) ||
        !convertPaint(b, call.second, *paint, *scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f)) {
        return;
    }
    b.calls.push_back(call);
}

void renderTriangles(void* user, NVGpaint* paint, NVGcompositeOperationState, NVGscissor* scissor,
                     const NVGvertex* verts, int nverts, float fringe) {
    Backend& b = backendOf(user);
    Call call;
    call.type = CallType::Triangles;
    call.triangleOffset = append(b, verts, nverts);
    call.triangleCount = nverts;
    if (!convertPaint(b, call.paint, *paint, *scissor, 1.0f, fringe, -1.0f)) {
        return;
    }
    call.paint.type = PaintType::TexturedTriangles;
    b.calls.push_back(call);
}

void drawStrokes(Backend& b, Band& band, const Call& call, const DrawState& state, const Paint& paint) {
    for (int i = 0; i < call.pathCount; ++i) {
        const PathRange& path = b.paths[static_cast<std::size_t>(call.pathOffset + i)];
        band.draw(*b.surface, state, paint, true, Primitive::Strip, b.vertices.data() + path.strokeOffset,
                  path.strokeCount);
    }
}

// glnvg__fill: winding into the stencil, fringes outside the shape, then the bounding quad where the stencil is set
void fill(Backend& b, Band& band, const Call& call) {
    DrawState winding;
    winding.writeColor = false;
    winding.cull = Cull::None;
    winding.passFront = StencilOp::IncrWrap;
    winding.passBack = StencilOp::DecrWrap;
    for (int i = 0; i < call.pathCount; ++i) {
        const PathRange& path = b.paths[static_cast<std::size_t>(call.pathOffset + i)];
        band.draw(*b.surface, winding, call.paint, true, Primitive::Fan, b.vertices.data() + path.fillOffset,
             path.fillCount);
    }
    DrawState fringes;
    fringes.test = StencilTest::EqualZero;
    drawStrokes(b, band, call, fringes, call.second);
    DrawState cover;
    cover.test = StencilTest::NotEqualZero;
    cover.fail = StencilOp::Zero;
    cover.passFront = StencilOp::Zero;
    cover.passBack = StencilOp::Zero;
    band.draw(*b.surface, cover, call.second, true, Primitive::Strip, b.vertices.data() + call.triangleOffset,
         call.triangleCount);
}

void convexFill(Backend& b, Band& band, const Call& call) {
    const DrawState state;
    for (int i = 0; i < call.pathCount; ++i) {
        const PathRange& path = b.paths[static_cast<std::size_t>(call.pathOffset + i)];
        band.draw(*b.surface, state, call.paint, true, Primitive::ConvexPolygon, b.vertices.data() + path.fillOffset,
                  path.fillCount);
        if (path.strokeCount > 0) {
            band.draw(*b.surface, state, call.paint, true, Primitive::Strip, b.vertices.data() + path.strokeOffset,
                 path.strokeCount);
        }
    }
}

// glnvg__stroke with stencil strokes: the base without overlap, the anti-aliased rim, then the stencil cleared
void stroke(Backend& b, Band& band, const Call& call) {
    DrawState base;
    base.test = StencilTest::EqualZero;
    base.passFront = StencilOp::Incr;
    base.passBack = StencilOp::Incr;
    drawStrokes(b, band, call, base, call.second);
    DrawState rim;
    rim.test = StencilTest::EqualZero;
    drawStrokes(b, band, call, rim, call.paint);
    DrawState clear;
    clear.writeColor = false;
    clear.fail = StencilOp::Zero;
    clear.passFront = StencilOp::Zero;
    clear.passBack = StencilOp::Zero;
    drawStrokes(b, band, call, clear, call.paint);
}

// diagnostics: cycles spent per kind of call, summed over all threads (FlushTimes::share)
std::atomic<std::uint64_t> gKindCycles[kCallKinds];

int callKind(const Call& call) {
    switch (call.type) {
    case CallType::ConvexFill:
        return call.paint.type == PaintType::Image ? 0 : 1;
    case CallType::Fill:
        return 2;
    case CallType::Stroke:
        return 3;
    case CallType::Triangles:
        return 4;
    }
    return 4;
}

void replay(Backend& b, Band& band) {
    std::uint64_t cycles[kCallKinds] = {};
    for (const Call& call : b.calls) {
        const std::uint64_t start = __builtin_ia32_rdtsc();
        switch (call.type) {
        case CallType::Fill:
            fill(b, band, call);
            break;
        case CallType::ConvexFill:
            convexFill(b, band, call);
            break;
        case CallType::Stroke:
            stroke(b, band, call);
            break;
        case CallType::Triangles:
            band.draw(*b.surface, DrawState{}, call.paint, true, Primitive::Triangles,
                      b.vertices.data() + call.triangleOffset, call.triangleCount);
            break;
        }
        cycles[callKind(call)] += __builtin_ia32_rdtsc() - start;
    }
    for (int k = 0; k < kCallKinds; ++k) {
        gKindCycles[k].fetch_add(cycles[k], std::memory_order_relaxed);
    }
}

std::mutex gTimesMutex;
FlushTimes gTimes;

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void renderFlush(void* user) {
    Backend& b = backendOf(user);
    if (!b.calls.empty()) {
        const auto start = std::chrono::steady_clock::now();
        FlushTimes times;
        times.threads = static_cast<int>(std::min<std::size_t>(b.workers ? b.threads : 1, 8));
        if (b.workers) {
            // each thread takes the next band not yet drawn: busy rows (posters, text) do not hold the others up
            std::atomic<std::size_t> next{0};
            b.workers->run(b.threads, [&b, &times, &next, start](std::size_t i) {
                for (std::size_t k = next.fetch_add(1); k < b.bands.size(); k = next.fetch_add(1)) {
                    replay(b, *b.bands[k]);
                }
                if (i < 8) {
                    times.busyMs[i] = millisecondsSince(start);
                }
            });
        } else {
            for (const auto& band : b.bands) {
                replay(b, *band);
            }
            times.busyMs[0] = millisecondsSince(start);
        }
        times.wallMs = millisecondsSince(start);
        std::uint64_t total = 0;
        std::uint64_t kinds[kCallKinds];
        for (int k = 0; k < kCallKinds; ++k) {
            kinds[k] = gKindCycles[k].exchange(0);
            total += kinds[k];
        }
        for (int k = 0; k < kCallKinds; ++k) {
            times.share[k] = total > 0 ? static_cast<int>(kinds[k] * 100 / total) : 0;
        }
        std::lock_guard<std::mutex> lock(gTimesMutex);
        gTimes = times;
    }
    renderCancel(user);
}

void renderDelete(void* user) { delete static_cast<Backend*>(user); }

constexpr int kBandsPerThread = 6;  // small bands: the threads finish together

int defaultThreads() {
#ifdef PLAYFIN_PS4
    return 4;  // of the 6 cores an app gets: main thread, 3 band threads, 2 workers
#else
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
#endif
}

}  // namespace

NVGcontext* createSoftwareCanvas(Surface& surface, int threadCount) {
    if (threadCount <= 0) {
        threadCount = defaultThreads();
    }
    const int threads = std::clamp(threadCount, 1, std::max(1, surface.height));
    const int count = std::min(threads * kBandsPerThread, std::max(1, surface.height));
    auto* backend = new Backend;  // owned by NanoVG: freed by renderDelete, also when creation fails
    backend->surface = &surface;
    backend->threads = static_cast<std::size_t>(threads);
    for (int i = 0; i < count; ++i) {
        backend->bands.push_back(std::make_unique<Band>(surface.height * i / count, surface.height * (i + 1) / count));
    }
    if (threads > 1) {
        backend->workers = std::make_unique<Workers>(static_cast<std::size_t>(threads - 1));
    }
    NVGparams params{};
    params.userPtr = backend;
    params.edgeAntiAlias = 1;
    params.renderCreate = renderCreate;
    params.renderCreateTexture = renderCreateTexture;
    params.renderDeleteTexture = renderDeleteTexture;
    params.renderUpdateTexture = renderUpdateTexture;
    params.renderGetTextureSize = renderGetTextureSize;
    params.renderViewport = renderViewport;
    params.renderCancel = renderCancel;
    params.renderFlush = renderFlush;
    params.renderFill = renderFill;
    params.renderStroke = renderStroke;
    params.renderTriangles = renderTriangles;
    params.renderDelete = renderDelete;
    return nvgCreateInternal(&params);
}


FlushTimes lastFlushTimes() {
    std::lock_guard<std::mutex> lock(gTimesMutex);
    return gTimes;
}

void destroySoftwareCanvas(NVGcontext* canvas) {
    if (canvas != nullptr) {
        nvgDeleteInternal(canvas);
    }
}

}  // namespace playfin::render
