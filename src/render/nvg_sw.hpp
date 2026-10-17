#pragma once

struct NVGcontext;

namespace playfin::render {

struct Surface;

// NanoVG drawing on the CPU into `surface` (kept by reference), with the look of the GLES2 backend
// (NVG_ANTIALIAS | NVG_STENCIL_STROKES). Calls are recorded and rasterized at nvgEndFrame, in horizontal bands
// shared by `threads` threads (0: 4 on the PS4, up to 4 elsewhere); the result does not depend on the count.
// nullptr on failure.
NVGcontext* createSoftwareCanvas(Surface& surface, int threads = 0);
void destroySoftwareCanvas(NVGcontext* canvas);

// Diagnostics: the last flush's wall time and each thread's busy time (thread 0 is the caller's).
constexpr int kCallKinds = 5;  // convex image fills, convex solid fills, other fills, strokes, triangles (text)
struct FlushTimes {
    double wallMs = 0;
    int threads = 0;
    double busyMs[8] = {};
    int share[kCallKinds] = {};  // percent of the threads' drawing time per kind
};
FlushTimes lastFlushTimes();

}  // namespace playfin::render
