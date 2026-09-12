// gl_core33.h -- a self-contained OpenGL 3.3 core-profile entry-point loader.
//
// Deliberately does NOT include the platform <GL/gl.h>: on Windows that header
// stops at GL 1.1, and on macOS it is the deprecated legacy header that errors
// out under a core profile. Everything the launcher uses is declared here and
// resolved at runtime through SDL_GL_GetProcAddress, which is the only portable
// way to reach 3.3 entry points on all three targets.
//
// Only the subset the presentation path needs is declared. Add to the X-macro
// list in gl_core33.cpp when you need more.

#pragma once

#include <cstddef>
#include <cstdint>

// ---- Types ---------------------------------------------------------------
using GLenum     = unsigned int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLbyte     = signed char;
using GLubyte    = unsigned char;
using GLshort    = short;
using GLushort   = unsigned short;
using GLint      = int;
using GLuint     = unsigned int;
using GLsizei    = int;
using GLfloat    = float;
using GLclampf   = float;
using GLdouble   = double;
using GLchar     = char;
using GLintptr   = std::intptr_t;
using GLsizeiptr = std::ptrdiff_t;

#if defined(_WIN32)
#    define GL_APIENTRY __stdcall
#else
#    define GL_APIENTRY
#endif

// ---- Constants -----------------------------------------------------------
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_NO_ERROR                       0
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_FRONT_AND_BACK                 0x0408
#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_UNPACK_SKIP_ROWS               0x0CF3
#define GL_UNPACK_SKIP_PIXELS             0x0CF4
#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_PACK_ALIGNMENT                 0x0D05
#define GL_TEXTURE_2D                     0x0DE1
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_FLOAT                          0x1406
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_FILL                           0x1B02
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_TEXTURE0                       0x84C0
#define GL_ARRAY_BUFFER                   0x8892
#define GL_STATIC_DRAW                    0x88E4
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_RGB8                           0x8051
#define GL_RGBA8                          0x8058
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C

// ---- Entry points --------------------------------------------------------
// Declared as function pointers; gl::load() fills them in. The `gl_` prefix plus
// a macro alias keeps call sites reading like ordinary GL.
#define SWOS_GL_FUNCTIONS(X)                                                                        \
    X(void,   Viewport,               (GLint x, GLint y, GLsizei w, GLsizei h))                     \
    X(void,   Scissor,                (GLint x, GLint y, GLsizei w, GLsizei h))                     \
    X(void,   ClearColor,             (GLclampf r, GLclampf g, GLclampf b, GLclampf a))             \
    X(void,   Clear,                  (GLbitfield mask))                                            \
    X(void,   Enable,                 (GLenum cap))                                                 \
    X(void,   Disable,                (GLenum cap))                                                 \
    X(void,   BlendFunc,              (GLenum sfactor, GLenum dfactor))                             \
    X(void,   DrawArrays,             (GLenum mode, GLint first, GLsizei count))                    \
    X(GLenum, GetError,               (void))                                                       \
    X(const GLubyte*, GetString,      (GLenum name))                                                \
    X(void,   PixelStorei,            (GLenum pname, GLint param))                                  \
    X(void,   ReadPixels,             (GLint x, GLint y, GLsizei w, GLsizei h,            \
                                       GLenum format, GLenum type, void *pixels))         \
    X(void,   GenTextures,            (GLsizei n, GLuint *textures))                                \
    X(void,   DeleteTextures,         (GLsizei n, const GLuint *textures))                          \
    X(void,   BindTexture,            (GLenum target, GLuint texture))                              \
    X(void,   ActiveTexture,          (GLenum texture))                                             \
    X(void,   TexParameteri,          (GLenum target, GLenum pname, GLint param))                   \
    X(void,   TexImage2D,             (GLenum target, GLint level, GLint internalformat,            \
                                       GLsizei width, GLsizei height, GLint border,                 \
                                       GLenum format, GLenum type, const void *pixels))             \
    X(void,   TexSubImage2D,          (GLenum target, GLint level, GLint xoffset, GLint yoffset,    \
                                       GLsizei width, GLsizei height, GLenum format,                \
                                       GLenum type, const void *pixels))                            \
    X(void,   GenVertexArrays,        (GLsizei n, GLuint *arrays))                                  \
    X(void,   DeleteVertexArrays,     (GLsizei n, const GLuint *arrays))                            \
    X(void,   BindVertexArray,        (GLuint array))                                               \
    X(void,   GenBuffers,             (GLsizei n, GLuint *buffers))                                 \
    X(void,   DeleteBuffers,          (GLsizei n, const GLuint *buffers))                           \
    X(void,   BindBuffer,             (GLenum target, GLuint buffer))                               \
    X(void,   BufferData,             (GLenum target, GLsizeiptr size, const void *data,            \
                                       GLenum usage))                                               \
    X(void,   VertexAttribPointer,    (GLuint index, GLint size, GLenum type, GLboolean normalized, \
                                       GLsizei stride, const void *pointer))                        \
    X(void,   EnableVertexAttribArray,(GLuint index))                                               \
    X(GLuint, CreateShader,           (GLenum type))                                                \
    X(void,   ShaderSource,           (GLuint shader, GLsizei count, const GLchar *const *string,   \
                                       const GLint *length))                                        \
    X(void,   CompileShader,          (GLuint shader))                                              \
    X(void,   GetShaderiv,            (GLuint shader, GLenum pname, GLint *params))                 \
    X(void,   GetShaderInfoLog,       (GLuint shader, GLsizei bufSize, GLsizei *length,             \
                                       GLchar *infoLog))                                            \
    X(void,   DeleteShader,           (GLuint shader))                                              \
    X(GLuint, CreateProgram,          (void))                                                       \
    X(void,   AttachShader,           (GLuint program, GLuint shader))                              \
    X(void,   LinkProgram,            (GLuint program))                                             \
    X(void,   GetProgramiv,           (GLuint program, GLenum pname, GLint *params))                \
    X(void,   GetProgramInfoLog,      (GLuint program, GLsizei bufSize, GLsizei *length,            \
                                       GLchar *infoLog))                                            \
    X(void,   UseProgram,             (GLuint program))                                             \
    X(void,   DeleteProgram,          (GLuint program))                                             \
    X(GLint,  GetUniformLocation,     (GLuint program, const GLchar *name))                         \
    X(void,   Uniform1i,              (GLint location, GLint v0))                                   \
    X(void,   Uniform2f,              (GLint location, GLfloat v0, GLfloat v1))                     \
    X(void,   Uniform4f,              (GLint location, GLfloat v0, GLfloat v1, GLfloat v2,          \
                                       GLfloat v3))

#define SWOS_GL_DECLARE(ret, name, args) extern ret(GL_APIENTRY *gl##name) args;
SWOS_GL_FUNCTIONS(SWOS_GL_DECLARE)
#undef SWOS_GL_DECLARE

namespace gl {

// Resolves every entry point above through SDL_GL_GetProcAddress. Requires a
// current GL context. Returns false (and reports the first missing symbol via
// missing_symbol()) if the driver does not supply the full 3.3 core subset.
bool load();

// Name of the first entry point load() could not resolve, or nullptr.
const char *missing_symbol();

} // namespace gl
