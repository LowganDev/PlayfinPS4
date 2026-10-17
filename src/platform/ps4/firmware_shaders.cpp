#include "platform/ps4/firmware_shaders.hpp"

#include <orbis/libkernel.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "core/log.hpp"

namespace playfin::platform::ps4 {

namespace {

// layout of the table exported by the module (OpenOrbis piglet sample)
struct ShaderBlob {
    const char* ident;
    unsigned char hash[16];
    std::uint64_t length;
    const unsigned char* code;
};

std::vector<FirmwareShader> loadShaders() {
    std::vector<FirmwareShader> shaders;
    const std::string path =
        std::string("/") + sceKernelGetFsSandboxRandomWord() + "/common/lib/libScePrecompiledShaders.sprx";
    int startResult = 0;
    const auto handle =
        static_cast<std::int32_t>(sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0, nullptr, &startResult));
    if (handle < 0) {
        PF_LOG_ERROR("gpu: cannot load %s (0x%08x)", path.c_str(), static_cast<unsigned>(handle));
        return shaders;
    }
    void* address = nullptr;
    if (sceKernelDlsym(handle, "scePrecompiledShaderEntries", &address) < 0 || address == nullptr) {
        PF_LOG_ERROR("gpu: no shader table in %s", path.c_str());
        return shaders;
    }
    for (const auto* entry = static_cast<const ShaderBlob*>(address); entry->ident != nullptr && shaders.size() < 1000;
         ++entry) {
        shaders.push_back({entry->ident, entry->code, static_cast<std::size_t>(entry->length)});
    }
    return shaders;
}

const FirmwareShader* findShader(const char* name) {
    for (const FirmwareShader& shader : firmwareShaders()) {
        if (std::strcmp(shader.name, name) == 0) {
            return &shader;
        }
    }
    return nullptr;
}

GLuint loadShader(GLenum type, const FirmwareShader& shader) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &count);
    std::vector<GLint> formats(static_cast<std::size_t>(std::max(count, 0)));
    if (!formats.empty()) {
        glGetIntegerv(GL_SHADER_BINARY_FORMATS, formats.data());
    } else {
        formats.push_back(0);
    }
    for (const GLint format : formats) {
        const GLuint id = glCreateShader(type);
        glGetError();
        glShaderBinary(1, &id, static_cast<GLenum>(format), shader.code, static_cast<GLsizei>(shader.size));
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            return id;
        }
        PF_LOG_WARN("gpu: %s with binary format 0x%x: error 0x%x", shader.name, static_cast<unsigned>(format),
                    static_cast<unsigned>(error));
        glDeleteShader(id);
    }
    return 0;
}

}  // namespace

const std::vector<FirmwareShader>& firmwareShaders() {
    static const std::vector<FirmwareShader> shaders = loadShaders();
    return shaders;
}

GLuint linkFirmwareProgram(const char* vertexName, const char* fragmentName) {
    const FirmwareShader* vertex = findShader(vertexName);
    const FirmwareShader* fragment = findShader(fragmentName);
    if (vertex == nullptr || fragment == nullptr) {
        PF_LOG_WARN("gpu: no firmware shader %s", vertex == nullptr ? vertexName : fragmentName);
        return 0;
    }
    const GLuint vs = loadShader(GL_VERTEX_SHADER, *vertex);
    const GLuint fs = loadShader(GL_FRAGMENT_SHADER, *fragment);
    if (vs == 0 || fs == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);  // freed with the program
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        char info[512] = {};
        glGetProgramInfoLog(program, sizeof(info) - 1, nullptr, info);
        PF_LOG_WARN("gpu: %s + %s do not link: %s", vertexName, fragmentName, info);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

std::vector<GLint> attributeLocations(GLuint program) {
    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    std::vector<GLint> locations;
    for (GLint i = 0; i < count; ++i) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(program, static_cast<GLuint>(i), sizeof(name), nullptr, &size, &type, name);
        const GLint location = glGetAttribLocation(program, name);
        if (location >= 0) {
            locations.push_back(location);
        }
    }
    std::sort(locations.begin(), locations.end());
    return locations;
}

void setNeutralUniforms(GLuint program) {
    static const float kIdentity4[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static const float kIdentity3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    glUseProgram(program);
    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(name), nullptr, &size, &type, name);
        const GLint location = glGetUniformLocation(program, name);
        if (location < 0) {
            continue;
        }
        switch (type) {
        case GL_FLOAT_MAT4:
            glUniformMatrix4fv(location, 1, GL_FALSE, kIdentity4);
            break;
        case GL_FLOAT_MAT3:
            glUniformMatrix3fv(location, 1, GL_FALSE, kIdentity3);
            break;
        case GL_SAMPLER_2D:
            glUniform1i(location, 0);
            break;
        case GL_FLOAT:
            glUniform1f(location, 1.0f);
            break;
        case GL_FLOAT_VEC2:
            glUniform2f(location, 1.0f, 1.0f);
            break;
        case GL_FLOAT_VEC3:
            glUniform3f(location, 1.0f, 1.0f, 1.0f);
            break;
        case GL_FLOAT_VEC4:
            glUniform4f(location, 1.0f, 1.0f, 1.0f, 1.0f);
            break;
        default:
            break;
        }
    }
}

void drawQuad(GLuint program, bool unitSquare, bool flipV) {
    const float low = unitSquare ? 0.0f : -1.0f;
    drawQuadAt(program, low, 1, 1, low, flipV);
}

void drawQuadAt(GLuint program, float left, float top, float right, float bottom, bool flipV) {
    static GLuint buffer = 0;
    if (buffer == 0) {
        glGenBuffers(1, &buffer);
    }
    const float vTop = flipV ? 1.0f : 0.0f;
    const float vBottom = flipV ? 0.0f : 1.0f;
    // triangle strip: top-left, top-right, bottom-left, bottom-right (clip space y up); then texture coordinates
    const float data[16] = {left, top, right, top, left, bottom, right, bottom,
                            0,    vTop, 1,   vTop, 0,    vBottom, 1,   vBottom};
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STREAM_DRAW);
    glUseProgram(program);
    const std::vector<GLint> locations = attributeLocations(program);
    for (std::size_t i = 0; i < locations.size() && i < 2; ++i) {
        const auto location = static_cast<GLuint>(locations[i]);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 2, GL_FLOAT, GL_FALSE, 0,
                              reinterpret_cast<const void*>(i * 8 * sizeof(float)));  // NOLINT: GL buffer offset
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (std::size_t i = 0; i < locations.size() && i < 2; ++i) {
        glDisableVertexAttribArray(static_cast<GLuint>(locations[i]));
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace playfin::platform::ps4
