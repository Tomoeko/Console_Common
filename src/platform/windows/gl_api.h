#ifndef CC_WINDOWS_GL_API_H
#define CC_WINDOWS_GL_API_H

#include <windows.h>
#include <GL/gl.h>
#include <stdbool.h>
#include <stddef.h>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_CLAMP_TO_EDGE 0x812F
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_HIGH_FLOAT 0x8DF2

/* Only the entry points used by the shared ES2 renderer are loaded. The
 * current WGL context owns their lifetime; all rendering stays on its thread. */
#define CC_GL_FUNCTIONS(X)                                                             \
    X(void, ActiveTexture, (GLenum texture))                                           \
    X(void, AttachShader, (GLuint program, GLuint shader))                             \
    X(void, BindAttribLocation, (GLuint program, GLuint index, const GLchar *name))    \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                      \
    X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer))                    \
    X(void, BlendFuncSeparate,                                                         \
      (GLenum source_rgb, GLenum destination_rgb, GLenum source_alpha,                 \
       GLenum destination_alpha))                                                      \
    X(void, BufferData,                                                                \
      (GLenum target, GLsizeiptr size, const void *data, GLenum usage))                \
    X(void, BufferSubData,                                                             \
      (GLenum target, GLintptr offset, GLsizeiptr size, const void *data))             \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                 \
    X(void, CompileShader, (GLuint shader))                                            \
    X(GLuint, CreateProgram, (void))                                                   \
    X(GLuint, CreateShader, (GLenum type))                                             \
    X(void, DeleteBuffers, (GLsizei count, const GLuint *buffers))                     \
    X(void, DeleteFramebuffers, (GLsizei count, const GLuint *framebuffers))           \
    X(void, DeleteProgram, (GLuint program))                                           \
    X(void, DeleteRenderbuffers, (GLsizei count, const GLuint *renderbuffers))         \
    X(void, DeleteShader, (GLuint shader))                                             \
    X(void, DisableVertexAttribArray, (GLuint index))                                  \
    X(void, EnableVertexAttribArray, (GLuint index))                                   \
    X(void, FramebufferRenderbuffer,                                                   \
      (GLenum target, GLenum attachment, GLenum renderbuffer_target,                   \
       GLuint renderbuffer))                                                           \
    X(void, FramebufferTexture2D,                                                      \
      (GLenum target, GLenum attachment, GLenum texture_target, GLuint texture,        \
       GLint level))                                                                   \
    X(void, GenBuffers, (GLsizei count, GLuint * buffers))                             \
    X(void, GenFramebuffers, (GLsizei count, GLuint * framebuffers))                   \
    X(void, GenRenderbuffers, (GLsizei count, GLuint * renderbuffers))                 \
    X(void, GetProgramInfoLog,                                                         \
      (GLuint program, GLsizei capacity, GLsizei * length, GLchar * log))              \
    X(void, GetProgramiv, (GLuint program, GLenum name, GLint * value))                \
    X(void, GetShaderInfoLog,                                                          \
      (GLuint shader, GLsizei capacity, GLsizei * length, GLchar * log))               \
    X(void, GetShaderiv, (GLuint shader, GLenum name, GLint * value))                  \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name))                 \
    X(void, LinkProgram, (GLuint program))                                             \
    X(void, RenderbufferStorage,                                                       \
      (GLenum target, GLenum format, GLsizei width, GLsizei height))                   \
    X(void, ShaderSource,                                                              \
      (GLuint shader, GLsizei count, const GLchar *const *strings,                     \
       const GLint *lengths))                                                          \
    X(void, Uniform1i, (GLint location, GLint value))                                  \
    X(void, Uniform2f, (GLint location, GLfloat first, GLfloat second))                \
    X(void, Uniform4f,                                                                 \
      (GLint location, GLfloat first, GLfloat second, GLfloat third, GLfloat fourth))  \
    X(void, Uniform4fv, (GLint location, GLsizei count, const GLfloat *values))        \
    X(void, UseProgram, (GLuint program))                                              \
    X(void, VertexAttribPointer,                                                       \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,    \
       const void *data))

typedef struct {
#define CC_GL_FIELD(result, name, arguments) result(APIENTRY *name) arguments;
    CC_GL_FUNCTIONS(CC_GL_FIELD)
#undef CC_GL_FIELD
} CcWindowsGl;

extern CcWindowsGl cc_windows_gl;
bool cc_windows_gl_load(void);

#define glActiveTexture cc_windows_gl.ActiveTexture
#define glAttachShader cc_windows_gl.AttachShader
#define glBindAttribLocation cc_windows_gl.BindAttribLocation
#define glBindBuffer cc_windows_gl.BindBuffer
#define glBindFramebuffer cc_windows_gl.BindFramebuffer
#define glBindRenderbuffer cc_windows_gl.BindRenderbuffer
#define glBlendFuncSeparate cc_windows_gl.BlendFuncSeparate
#define glBufferData cc_windows_gl.BufferData
#define glBufferSubData cc_windows_gl.BufferSubData
#define glCheckFramebufferStatus cc_windows_gl.CheckFramebufferStatus
#define glCompileShader cc_windows_gl.CompileShader
#define glCreateProgram cc_windows_gl.CreateProgram
#define glCreateShader cc_windows_gl.CreateShader
#define glDeleteBuffers cc_windows_gl.DeleteBuffers
#define glDeleteFramebuffers cc_windows_gl.DeleteFramebuffers
#define glDeleteProgram cc_windows_gl.DeleteProgram
#define glDeleteRenderbuffers cc_windows_gl.DeleteRenderbuffers
#define glDeleteShader cc_windows_gl.DeleteShader
#define glDisableVertexAttribArray cc_windows_gl.DisableVertexAttribArray
#define glEnableVertexAttribArray cc_windows_gl.EnableVertexAttribArray
#define glFramebufferRenderbuffer cc_windows_gl.FramebufferRenderbuffer
#define glFramebufferTexture2D cc_windows_gl.FramebufferTexture2D
#define glGenBuffers cc_windows_gl.GenBuffers
#define glGenFramebuffers cc_windows_gl.GenFramebuffers
#define glGenRenderbuffers cc_windows_gl.GenRenderbuffers
#define glGetProgramInfoLog cc_windows_gl.GetProgramInfoLog
#define glGetProgramiv cc_windows_gl.GetProgramiv
#define glGetShaderInfoLog cc_windows_gl.GetShaderInfoLog
#define glGetShaderiv cc_windows_gl.GetShaderiv
#define glGetUniformLocation cc_windows_gl.GetUniformLocation
#define glLinkProgram cc_windows_gl.LinkProgram
#define glRenderbufferStorage cc_windows_gl.RenderbufferStorage
#define glShaderSource cc_windows_gl.ShaderSource
#define glUniform1i cc_windows_gl.Uniform1i
#define glUniform2f cc_windows_gl.Uniform2f
#define glUniform4f cc_windows_gl.Uniform4f
#define glUniform4fv cc_windows_gl.Uniform4fv
#define glUseProgram cc_windows_gl.UseProgram
#define glVertexAttribPointer cc_windows_gl.VertexAttribPointer

static inline void glClearDepthf(GLfloat depth) {
    glClearDepth((GLdouble)depth);
}

static inline void glGetShaderPrecisionFormat(GLenum stage, GLenum kind, GLint range[2],
                                              GLint *precision) {
    (void)stage;
    (void)kind;
    range[0] = 127;
    range[1] = 127;
    *precision = 23;
}

#endif
