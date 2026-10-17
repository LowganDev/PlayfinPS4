// Diagnostics for the console: what Piglet offers for shaders, which firmware programs can show a texture, and
// how long a full CPU frame takes to fill and upload. Reads, draws a test pattern and logs; changes nothing else.
#include <GLES2/gl2.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <vector>

#include "core/log.hpp"
#include "platform/ps4/firmware_shaders.hpp"
#include "platform/ps4/gpu_probe_ps4.hpp"

namespace playfin::platform::ps4 {

namespace {

const char* glText(GLenum name) {
    const auto* text = reinterpret_cast<const char*>(glGetString(name));
    return text != nullptr ? text : "(null)";
}

double nowMs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1.0e6;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0.0 : values[values.size() / 2];
}

void logInterface(GLuint program, const char* label) {
    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    for (GLint i = 0; i < count; ++i) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(program, static_cast<GLuint>(i), sizeof(name), nullptr, &size, &type, name);
        PF_LOG_INFO("gpu: %s attribute %s type 0x%x size %d location %d", label, name, static_cast<unsigned>(type),
                    size, glGetAttribLocation(program, name));
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(name), nullptr, &size, &type, name);
        PF_LOG_INFO("gpu: %s uniform %s type 0x%x size %d", label, name, static_cast<unsigned>(type), size);
    }
}

unsigned pixelAt(int x, int y) {
    std::uint8_t p[4] = {};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (static_cast<unsigned>(p[0]) << 24) | (static_cast<unsigned>(p[1]) << 16) |
           (static_cast<unsigned>(p[2]) << 8) | p[3];
}

// Draws a 2x2 texture (red, green / blue, white, top row first) over the screen and reads the four quadrants.
// Expected when the program and mode are right: TL ff0000ff TR 00ff00ff BL 0000ffff BR ffffffff.
void tryPattern(GLuint program, const char* label, bool unitSquare) {
    const std::uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glViewport(0, 0, 1920, 1080);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glGetError();
    setNeutralUniforms(program);
    drawQuad(program, unitSquare, false);
    glFinish();
    const GLenum error = glGetError();
    // window coordinates: y up, so the top quadrants are at y = 810
    PF_LOG_INFO("gpu: pattern %s %s: TL %08x TR %08x BL %08x BR %08x error 0x%x", label,
                unitSquare ? "unit-square" : "ndc", pixelAt(480, 810), pixelAt(1440, 810), pixelAt(480, 270),
                pixelAt(1440, 270), static_cast<unsigned>(error));
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
}

void probePrograms() {
    struct Candidate {
        const char* vertex;
        const char* fragment;
    };
    const Candidate candidates[] = {
        {"texmap/v_0.vert", "texmap/f_0.frag"},
        {"texmap/v_1.vert", "texmap/f_1.frag"},
        {"texmap/v_2.vert", "texmap/f_2.frag"},
        {"swordfish/vpBasic.vert", "swordfish/fpAlphaTexture.frag"},
        {"swordfish/vpBasic.vert", "swordfish/fpSolidColor.frag"},
        {"swordfish/vpTexClipZoom.vert", "swordfish/fpAlphaTexture.frag"},
        {"trilithium/vpBasic.vert", "trilithium/fpTexture.frag"},
    };
    for (const Candidate& candidate : candidates) {
        const GLuint program = linkFirmwareProgram(candidate.vertex, candidate.fragment);
        char label[160];
        std::snprintf(label, sizeof(label), "%s+%s", candidate.vertex, candidate.fragment);
        if (program == 0) {
            PF_LOG_INFO("gpu: program %s unusable", label);
            continue;
        }
        logInterface(program, label);
        tryPattern(program, label, false);
        tryPattern(program, label, true);
        glUseProgram(0);
        glDeleteProgram(program);
    }
}

void probeTimings() {
    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(kWidth) * kHeight * 4);
    std::vector<double> fill;
    for (int round = 0; round < 30; ++round) {
        const double start = nowMs();
        for (std::size_t i = 0; i < frame.size(); i += 4) {
            frame[i] = static_cast<std::uint8_t>(i >> 4);
            frame[i + 1] = static_cast<std::uint8_t>(round);
            frame[i + 2] = static_cast<std::uint8_t>(i >> 12);
            frame[i + 3] = 255;
        }
        fill.push_back(nowMs() - start);
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    std::vector<double> upload;
    for (int round = 0; round < 30; ++round) {
        const double start = nowMs();
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
        glFinish();
        upload.push_back(nowMs() - start);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
    PF_LOG_INFO("gpu: full frame CPU fill %.2f ms, upload %.2f ms (medians of 30)", median(fill), median(upload));
}

}  // namespace

void probeGpu() {
    PF_LOG_INFO("gpu: %s / %s / %s", glText(GL_VENDOR), glText(GL_RENDERER), glText(GL_VERSION));
    GLboolean compiler = GL_FALSE;
    glGetBooleanv(GL_SHADER_COMPILER, &compiler);
    GLint formats = 0;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &formats);
    PF_LOG_INFO("gpu: shader compiler %d, binary formats %d, firmware shaders %d", compiler, formats,
                static_cast<int>(firmwareShaders().size()));
    probePrograms();
    probeTimings();
}

}  // namespace playfin::platform::ps4
