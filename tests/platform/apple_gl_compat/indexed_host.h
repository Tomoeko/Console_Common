#ifndef CC_TEST_INDEXED_APPLE_HOST_H
#define CC_TEST_INDEXED_APPLE_HOST_H

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

/* The production GLES2 entrypoints run on Apple's GL driver. Only framebuffer
 * zero is adapted to this context's explicit drawable; offscreen targets use
 * the unchanged native framebuffer operations. */
void cc_test_bind_framebuffer(GLenum target, GLuint framebuffer);
#define glBindFramebuffer cc_test_bind_framebuffer
#define glClearDepthf glClearDepth

#endif
