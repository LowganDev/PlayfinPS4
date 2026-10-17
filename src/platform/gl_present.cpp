// Linux: the CPU frame as one texture drawn by a GLSL quad (Mesa compiles shaders; the PS4 does not, see
// ps4/present_ps4.cpp); a video layer is a second texture drawn letterboxed under it.
#include <GLES2/gl2.h>

#include "core/log.hpp"
#include "platform/platform.hpp"
#include "render/composite.hpp"

namespace playfin::platform {

namespace {

constexpr const char* kVertex =
    "attribute vec4 a_vertex;\n"  // clip-space position, texture coordinates
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_vertex.zw;\n"
    "    gl_Position = vec4(a_vertex.xy, 0.0, 1.0);\n"
    "}\n";

constexpr const char* kFragment =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_frame;\n"
    "void main() { gl_FragColor = texture2D(u_frame, v_uv); }\n";

struct Presenter {
    GLuint program = 0;
    GLuint texture = 0;
    GLuint video = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    GLuint buffer = 0;
    GLint position = -1;
    GLint sampler = -1;
    int width = 0;
    int height = 0;
    bool failed = false;
};

Presenter gPresenter;

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char info[512] = {};
        glGetShaderInfoLog(shader, sizeof(info) - 1, nullptr, info);
        PF_LOG_ERROR("present: shader does not compile: %s", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool setup(Presenter& p, int width, int height) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertex);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragment);
    if (vertex == 0 || fragment == 0) {
        return false;
    }
    p.program = glCreateProgram();
    glAttachShader(p.program, vertex);
    glAttachShader(p.program, fragment);
    glLinkProgram(p.program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        PF_LOG_ERROR("present: program does not link");
        return false;
    }
    p.position = glGetAttribLocation(p.program, "a_vertex");
    p.sampler = glGetUniformLocation(p.program, "u_frame");
    glGenBuffers(1, &p.buffer);
    glGenTextures(1, &p.texture);
    glBindTexture(GL_TEXTURE_2D, p.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    p.width = width;
    p.height = height;
    return p.position >= 0;
}

// A textured quad from (left, top) to (right, bottom) in clip space (y up).
void drawQuad(const Presenter& p, GLuint texture, float left, float top, float right, float bottom) {
    const float data[16] = {left, top, 0, 0, right, top, 1, 0, left, bottom, 0, 1, right, bottom, 1, 1};
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindBuffer(GL_ARRAY_BUFFER, p.buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STREAM_DRAW);
    glEnableVertexAttribArray(static_cast<GLuint>(p.position));
    glVertexAttribPointer(static_cast<GLuint>(p.position), 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(p.position));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

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
    drawQuad(p, p.video, box.x / sw * 2 - 1, 1 - box.y / sh * 2, (box.x + box.width) / sw * 2 - 1,
             1 - (box.y + box.height) / sh * 2);
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
    const Size size = drawableSize();
    glViewport(0, 0, size.width, size.height);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.texture);
    if (changed) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glUseProgram(p.program);
    glUniform1i(p.sampler, 0);
    if (video != nullptr) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        drawVideo(p, *video, size.width, size.height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // the UI frame is premultiplied
    }
    drawQuad(p, p.texture, -1, 1, 1, -1);
    glDisable(GL_BLEND);
    swap();
}

}  // namespace playfin::platform
