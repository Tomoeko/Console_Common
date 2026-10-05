#include <OpenGL/OpenGL.h>
#include "apple_gl_compat/indexed_host.h"
#include "../support/indexed_mesh_fixture.h"
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

static void mesh_update_failure(MeshFixture *fixture) {
    CcIndexedTargetDescription description = {.width = 4, .height = 4};
    CcIndexedTarget *target =
        cc_indexed_target_create(fixture->renderer, &description, NULL, 0);
    CHECK(target);
    CHECK(cc_indexed_reserve_passes(fixture->renderer, 2, NULL, 0));
    CcIndexedFrame frame = {.clear_color_enabled = true};
    CcIndexedPass pass = {
        .target = target, .viewport = {0, 0, 4, 4}, .color_load = CC_INDEXED_CLEAR};
    CHECK(cc_indexed_begin_passes(fixture->renderer, &frame, NULL, 0));
    CHECK(cc_indexed_pass_begin(fixture->renderer, &pass, NULL, 0));
    glEnable((GLenum)UINT32_MAX);
    CHECK(!cc_indexed_mesh_update(fixture->renderer, fixture->mesh, fixture->vertices,
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(!cc_indexed_mesh_update(fixture->renderer, fixture->mesh, fixture->vertices,
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(!cc_indexed_pass_begin(fixture->renderer, &pass, NULL, 0));
    CHECK(!cc_indexed_end(fixture->renderer, NULL, 0));
    CHECK(cc_indexed_begin_passes(fixture->renderer, &frame, NULL, 0));
    pass.color_load = CC_INDEXED_LOAD;
    CHECK(!cc_indexed_pass_begin(fixture->renderer, &pass, NULL, 0));
    pass.target = NULL;
    pass.viewport = (CcViewport){0, 0, 128, 96};
    pass.color_load = CC_INDEXED_CLEAR;
    CHECK(cc_indexed_pass_begin(fixture->renderer, &pass, NULL, 0));
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_mesh_update(fixture->renderer, fixture->mesh, fixture->vertices,
                                 sizeof(fixture->vertices), NULL, 0));
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_end(fixture->renderer, NULL, 0));
    CHECK(cc_indexed_target_release(fixture->renderer, &target, NULL, 0));

    /* Outside a frame, an entered upload failure still requires a full repair;
     * it does not poison the next frame's unrelated command ownership. */
    glEnable((GLenum)UINT32_MAX);
    CHECK(!cc_indexed_mesh_update(fixture->renderer, fixture->mesh, fixture->vertices,
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(cc_indexed_begin(fixture->renderer, &frame, NULL, 0));
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_mesh_update(fixture->renderer, fixture->mesh, fixture->vertices,
                                 sizeof(fixture->vertices), NULL, 0));
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
    MeshFixture fixture;
    mesh_fixture_open(&fixture, renderer);
    CcIndexedRenderer *other = cc_indexed_create(&platform, NULL, 0);
    CHECK(other);
    CHECK(!cc_indexed_mesh_update(other, fixture.mesh, fixture.vertices,
                                  sizeof(fixture.vertices), NULL, 0));
    cc_indexed_destroy(other);
    mesh_update_failure(&fixture);
    for (unsigned iteration = 0; iteration < 12; ++iteration) {
        bool right = (iteration & 1) != 0;
        uint8_t color[4] = {37, (uint8_t)(iteration * 19), 113, 255};
        mesh_frame(&fixture, (iteration & 2) != 0, right, color);
        uint8_t pixel[4];
        glReadPixels(right ? 96 : 32, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        mesh_pixels(pixel, color);
        const uint8_t black[4] = {0, 0, 0, 255};
        glReadPixels(right ? 32 : 96, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        mesh_pixels(pixel, black);
    }
    const uint8_t final_color[4] = {83, 127, 191, 255};
    mesh_frame(&fixture, true, false, final_color);
    mesh_fixture_close(&fixture);
    cc_indexed_destroy(renderer);
    glDeleteFramebuffers(1, &host.framebuffer);
    glDeleteTextures(1, &host.texture);
    CHECK(glGetError() == GL_NO_ERROR);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(host.context);
    puts("Production GLES2 dynamic mesh contents, use gate and failure repair passed.");
    return EXIT_SUCCESS;
}
