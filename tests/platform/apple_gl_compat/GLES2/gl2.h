#ifndef CC_TEST_APPLE_GL_COMPAT_H
#define CC_TEST_APPLE_GL_COMPAT_H

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#define GL_HIGH_FLOAT 0x8df2
void glGetShaderPrecisionFormat(GLenum type, GLenum precision, GLint range[2],
                                GLint *bits);

#endif
