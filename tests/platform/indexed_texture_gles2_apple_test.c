#include <OpenGL/OpenGL.h>
#include "apple_gl_compat/indexed_host.h"
#include "../support/indexed_texture_fixture.h"
#include "../support/indexed_partial_texture_fixture.h"
#include "host.h"

#undef glBindFramebuffer

struct CcGles2Host {
    CGLContextObj context;
    GLuint framebuffer;
    GLuint texture;
};

struct CcPlatform {
    CcGles2Host *host;
};

static CcGles2Host *test_host;

void cc_test_bind_framebuffer(GLenum target, GLuint framebuffer) {
    glBindFramebuffer(target, framebuffer ? framebuffer : test_host->framebuffer);
}

CcGles2Host *cc_gles2_platform_host(CcPlatform *platform) {
    return platform->host;
}

void cc_gles2_platform_invalidate_graphics(CcPlatform *platform) {
    (void)platform;
}

bool cc_gles2_host_make_current(CcGles2Host *host) {
    return CGLSetCurrentContext(host->context) == kCGLNoError;
}

void cc_gles2_host_surface_size(CcGles2Host *host, int *width, int *height) {
    (void)host;
    *width = 128;
    *height = 96;
}

bool cc_gles2_host_present(CcGles2Host *host) {
    (void)host;
    return true;
}

static void texture_update_failure(TextureFixture *fixture) {
    CcIndexedFrame frame = {.clear_color_enabled = true};
    CHECK(cc_indexed_begin(fixture->renderer, &frame, NULL, 0));
    glEnable((GLenum)UINT32_MAX);
    CHECK(!cc_indexed_texture_update(fixture->renderer, fixture->texture, 0,
                                     fixture->levels, 64, NULL, 0));
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(!cc_indexed_texture_update(fixture->renderer, fixture->texture, 0,
                                     fixture->levels, 64, NULL, 0));
    CHECK(!cc_indexed_end(fixture->renderer, NULL, 0));
    CHECK(cc_indexed_begin(fixture->renderer, &frame, NULL, 0));
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_texture_update(fixture->renderer, fixture->texture, 0,
                                    fixture->levels, 64, NULL, 0));
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_end(fixture->renderer, NULL, 0));
}

int main(void) {
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFAColorSize, 24,
                                            (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj format = NULL;
    GLint count = 0;
    if (CGLChoosePixelFormat(attributes, &format, &count) != kCGLNoError || !format)
        return 77;
    CcGles2Host host = {0};
    CGLError result = CGLCreateContext(format, NULL, &host.context);
    CGLDestroyPixelFormat(format);
    if (result != kCGLNoError || !host.context)
        return 77;
    test_host = &host;
    CHECK(cc_gles2_host_make_current(&host));
    glGenTextures(1, &host.texture);
    glBindTexture(GL_TEXTURE_2D, host.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 96, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 NULL);
    glGenFramebuffers(1, &host.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, host.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           host.texture, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    CcPlatform platform = {&host};
    CcIndexedRenderer *renderer = cc_indexed_create(&platform, NULL, 0);
    CHECK(renderer);
    uint8_t partial_pixels[PARTIAL_MIPS_BYTES];
    CcIndexedTextureDescription partial = partial_texture_description(partial_pixels);
    CHECK(!cc_indexed_texture_create(renderer, &partial, NULL, 0));
    CHECK(!cc_indexed_texture_create_dynamic(renderer, &partial, NULL, 0));
    CHECK(glGetError() == GL_NO_ERROR);
    TextureFixture fixture;
    texture_fixture_open(&fixture, renderer);
    CcIndexedRenderer *other = cc_indexed_create(&platform, NULL, 0);
    CHECK(other);
    CHECK(!cc_indexed_texture_update(other, fixture.texture, 0, fixture.levels, 64,
                                     NULL, 0));
    cc_indexed_destroy(other);
    texture_update_failure(&fixture);
    for (unsigned iteration = 0; iteration < 12; ++iteration) {
        uint8_t color[4] = {37, (uint8_t)(iteration * 19), 113, 255};
        texture_frame(&fixture, (iteration & 1) != 0, iteration < 6 ? 0 : 2, color);
        uint8_t pixel[4];
        glReadPixels(64, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CcFramebuffer frame = {pixel, 1, 1, 4};
        texture_pixels(&frame, color);
    }
    const uint8_t final_color[4] = {83, 127, 191, 255};
    texture_frame(&fixture, true, 0, final_color);
    /* Native commands must keep the just-submitted GPU resource alive while
     * the application handle and CPU staging retire without an explicit wait. */
    texture_fixture_close(&fixture);
    cc_indexed_destroy(renderer);
    glDeleteFramebuffers(1, &host.framebuffer);
    glDeleteTextures(1, &host.texture);
    CHECK(glGetError() == GL_NO_ERROR);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(host.context);
    puts("Production GLES2 dynamic texture contents, use gate and failure repair "
         "passed.");
    return EXIT_SUCCESS;
}
