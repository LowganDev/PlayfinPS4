#pragma once

#include <GLES2/gl2.h>

#include <cstddef>
#include <vector>

namespace playfin::platform::ps4 {

// One compiled shader of the console's libScePrecompiledShaders.sprx (WebKit and system UI shaders), loaded at
// run time from the console: nothing Sony-made is shipped.
struct FirmwareShader {
    const char* name;
    const unsigned char* code;
    std::size_t size;
};

// All shaders of the module (loaded once; empty when it cannot be loaded).
const std::vector<FirmwareShader>& firmwareShaders();

// glShaderBinary of two firmware shaders, then link; 0 on failure (logged).
GLuint linkFirmwareProgram(const char* vertexName, const char* fragmentName);

// The program's active attribute locations, ascending.
std::vector<GLint> attributeLocations(GLuint program);

// Every active uniform set to a neutral value: matrices identity, samplers texture unit 0, floats and vectors 1.
void setNeutralUniforms(GLuint program);

// A quad over the whole viewport (triangle strip). First attribute: positions, -1..1 or, with unitSquare, 0..1.
// Second attribute (if any): texture coordinates 0..1, v = 0 at the top unless flipV.
void drawQuad(GLuint program, bool unitSquare, bool flipV);

// The same with the quad from (left, top) to (right, bottom) in clip space (y up).
void drawQuadAt(GLuint program, float left, float top, float right, float bottom, bool flipV);

}  // namespace playfin::platform::ps4
