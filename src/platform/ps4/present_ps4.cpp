// PS4: the CPU frame as one texture drawn with a firmware shader program (retail firmware cannot compile shaders;
// the console's own precompiled shaders are used, nothing is shipped).
#include <GLES2/gl2.h>

#include "core/log.hpp"
#include "platform/platform.hpp"
#include "platform/ps4/firmware_shaders.hpp"
#include "render/composite.hpp"

namespace playfin::platform {

namespace {

// The firmware shaders that draw a texture upright (docs/ps4-notes.md).
constexpr const char* kVertexShader = "swordfish/vpBasic.vert";
constexpr const char* kFragmentShader = "swordfish/fpAlphaTexture.frag";
constexpr bool kUnitSquare = false;
constexpr bool kFlipV = false;

struct Presenter {
    GLuint program = 0;
    GLuint texture = 0;
    GLuint video = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int width = 0;
    int height = 0;
    bool failed = false;
    int frames = 0;
};

Presenter gPresenter;

bool setup(Presenter& p, int width, int height) {
    p.program = ps4::linkFirmwareProgram(kVertexShader, kFragmentShader);
    if (p.program == 0) {
        PF_LOG_ERROR("present: firmware program %s + %s unavailable", kVertexShader, kFragmentShader);
        return false;
    }
    ps4::setNeutralUniforms(p.program);
    glGenTextures(1, &p.texture);
    glBindTexture(GL_TEXTURE_2D, p.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    p.width = width;
    p.height = height;
    PF_LOG_INFO("present: %s + %s", kVertexShader, kFragmentShader);
    return true;
}

// The video texture (GPU-scaled, linear) letterboxed on the screen.
void drawVideo(Presenter& p, const VideoLayer& video, int screenWidth, int screenHeight) {
    if (video.rgb0 == nullptr || video.width <= 0 || video.height <= 0) {
        return;
    }
    if (p.video == 0) {
        glGenTextures(1, &p.video);
    }
    glBindTexture(GL_TEXTURE_2D, p.video);
    const bool fresh = p.videoWidth != video.width || p.videoHeight != video.height;
    if (fresh) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, video.width, video.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        p.videoWidth = video.width;
        p.videoHeight = video.height;
    }
    if (video.changed || fresh) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video.width, video.height, GL_RGBA, GL_UNSIGNED_BYTE, video.rgb0);
    }
    const render::Box box = render::letterbox(video.width, video.height, screenWidth, screenHeight);
    const float sw = static_cast<float>(screenWidth);
    const float sh = static_cast<float>(screenHeight);
    ps4::drawQuadAt(p.program, box.x / sw * 2 - 1, 1 - box.y / sh * 2, (box.x + box.width) / sw * 2 - 1,
                    1 - (box.y + box.height) / sh * 2, kFlipV);
}

}  // namespace

void presentFrame(const std::uint8_t* rgba, int width, int height, bool changed, const VideoLayer* video) {
    Presenter& p = gPresenter;
    if (p.failed) {
        swap();
        return;
    }
    if (p.program == 0 || p.width != width || p.height != height) {
        if (!setup(p, width, height)) {
            p.failed = true;
            swap();
            return;
        }
        changed = true;
    }
    Size size = drawableSize();
    if (p.frames == 0) {
        PF_LOG_INFO("present: drawable %dx%d", size.width, size.height);
    }
    if (size.width <= 0 || size.height <= 0) {
        size = {width, height};  // the frame is made for the 1920x1080 output
    }
    glViewport(0, 0, size.width, size.height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    if (video != nullptr) {
        drawVideo(p, *video, size.width, size.height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // the UI frame is premultiplied
    }
    glBindTexture(GL_TEXTURE_2D, p.texture);
    if (changed) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    ps4::drawQuad(p.program, kUnitSquare, kFlipV);
    glDisable(GL_BLEND);
    ++p.frames;
    swap();
}

}  // namespace playfin::platform
