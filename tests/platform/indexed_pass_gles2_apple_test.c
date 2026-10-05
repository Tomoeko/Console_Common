#include <OpenGL/OpenGL.h>
#include "apple_gl_compat/indexed_host.h"
#include "../support/indexed_pass_fixture.h"
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

static void entered_pass_failure(CcIndexedRenderer *renderer) {
    CcIndexedTarget *target = pass_target(renderer, CC_INDEXED_RGBA8, false);
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    CcIndexedPass pass = pass_description(target, CC_INDEXED_CLEAR);
    pass.scissor_enabled = true;
    pass.viewport = (CcViewport){0, -16, 32, 32};
    pass.scissor = (CcViewport){0, 8, 32, 16};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    /* A real driver error is observed only after the next pass has entered its
     * native scope. The earlier logical pass must not remain drawable. */
    glEnable((GLenum)UINT32_MAX);
    pass.clear_color = (CcColor){1, 0, 0, 1};
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    CcIndexedDraw unavailable = {.program = (CcIndexedProgram *)(uintptr_t)8,
                                 .mesh = (CcIndexedMesh *)(uintptr_t)8};
    PASS_REQUIRE(!cc_indexed_draw(renderer, &unavailable, NULL, 0));
    pass.target = NULL;
    pass.viewport = (CcViewport){0, 0, 1, 1};
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(!cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    pass = pass_description(target, CC_INDEXED_LOAD);
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(!cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &target, NULL, 0));
}

static void viewport_capability(CcIndexedRenderer *renderer) {
    GLint limits[2];
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, limits);
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    CcIndexedPass pass = {.viewport = {0, 0, limits[0], limits[1]},
                          .color_load = CC_INDEXED_CLEAR,
                          .depth_load = CC_INDEXED_DISCARD,
                          .scissor_enabled = true,
                          .scissor = {0, 0, 128, 96}};
    GLint original[4];
    glGetIntegerv(GL_VIEWPORT, original);
    if (limits[0] < INT_MAX) {
        pass.viewport.width = limits[0] + 1;
        PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
        GLint current[4];
        glGetIntegerv(GL_VIEWPORT, current);
        for (size_t lane = 0; lane < 4; ++lane)
            PASS_REQUIRE(current[lane] == original[lane]);
    }
    pass.viewport = (CcViewport){0, 0, 128, 96};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
}

static void capture_pixels(uint8_t *pixels, uint8_t *top_rows) {
    glReadPixels(0, 0, 128, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (size_t y = 0; y < 96; ++y) {
        for (size_t byte = 0; byte < 128 * 4; ++byte)
            top_rows[y * 128 * 4 + byte] = pixels[(95 - y) * 128 * 4 + byte];
    }
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
    PASS_REQUIRE(cc_gles2_host_make_current(&host));
    glGenTextures(1, &host.texture);
    glBindTexture(GL_TEXTURE_2D, host.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 96, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 NULL);
    glGenFramebuffers(1, &host.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, host.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           host.texture, 0);
    PASS_REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    CcPlatform platform = {&host};
    CcIndexedRenderer *renderer = cc_indexed_create(&platform, NULL, 0);
    PASS_REQUIRE(renderer);
    CcIndexedRenderer *other = cc_indexed_create(&platform, NULL, 0);
    PASS_REQUIRE(other);
    pass_admission(renderer, other);
    cc_indexed_destroy(other);
    entered_pass_failure(renderer);
    viewport_capability(renderer);
    CcIndexedTargetDescription unsupported = {
        .width = 8, .height = 8, .color_format = CC_INDEXED_RGBA16_FLOAT};
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &unsupported, NULL, 0));
    uint8_t pixels[128 * 96 * 4];
    uint8_t top_rows[sizeof(pixels)];
    for (size_t iteration = 0; iteration < 4; ++iteration) {
        pass_scene(renderer, 128, 96, false);
        capture_pixels(pixels, top_rows);
        CcFramebuffer frame = {top_rows, 128, 96, 128 * 4};
        pass_pixels(&frame);
    }
    const int sizes[2][2] = {{1280, 720}, {720, 576}};
    for (size_t index = 0; index < 2; ++index) {
        pass_extended_scene(renderer, 128, 96, sizes[index][0], sizes[index][1]);
        capture_pixels(pixels, top_rows);
        CcFramebuffer frame = {top_rows, 128, 96, 128 * 4};
        pass_extended_pixels(&frame, sizes[index][1]);
    }
    PASS_REQUIRE(cc_indexed_wait(renderer, NULL, 0));
    cc_indexed_destroy(renderer);
    glDeleteFramebuffers(1, &host.framebuffer);
    glDeleteTextures(1, &host.texture);
    PASS_REQUIRE(glGetError() == GL_NO_ERROR);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(host.context);
    puts("Production GLES2 RGBA8 ordered GPU passes passed on Apple GL.");
    return EXIT_SUCCESS;
}
