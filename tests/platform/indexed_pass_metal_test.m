#include "../support/indexed_shared_depth_fixture.h"
#include "../../src/platform/metal/platform_metal_internal.h"

static void test_drawable_size(CcPlatform *platform, CcIndexedRenderer *renderer,
                               int width, int height) {
    CcMetalState *native = cc_metal_platform_state(platform);
    NSSize points = native->view.bounds.size;
    NSSize backing = [native->view convertSizeToBacking:points];
    [native->window setContentSize:NSMakeSize(points.width * width / backing.width,
                                              points.height * height / backing.height)];
    PASS_REQUIRE(cc_indexed_prepare_drawable_depth(renderer, NULL, 0));
    CcFramebuffer shared_frame;
    PASS_REQUIRE(cc_platform_capture_begin(platform, &shared_frame));
    PASS_REQUIRE(shared_frame.width == width && shared_frame.height == height);
    shared_depth_scene(renderer, shared_frame.width, shared_frame.height);
    PASS_REQUIRE(cc_platform_capture_frame(platform, &shared_frame));
    shared_depth_pixels(&shared_frame);
    cc_platform_capture_end(platform);
    for (size_t iteration = 0; iteration < 4; ++iteration) {
        CcFramebuffer frame;
        PASS_REQUIRE(cc_platform_capture_begin(platform, &frame));
        pass_scene(renderer, frame.width, frame.height, (iteration & 1) != 0);
        PASS_REQUIRE(cc_platform_capture_frame(platform, &frame));
        pass_pixels(&frame);
        cc_platform_capture_end(platform);
    }
    const int sizes[2][2] = {{1280, 720}, {720, 576}};
    for (size_t index = 0; index < 2; ++index) {
        CcFramebuffer frame;
        PASS_REQUIRE(cc_platform_capture_begin(platform, &frame));
        pass_extended_scene(renderer, frame.width, frame.height, sizes[index][0],
                            sizes[index][1]);
        PASS_REQUIRE(cc_platform_capture_frame(platform, &frame));
        pass_extended_pixels(&frame, sizes[index][1]);
        cc_platform_capture_end(platform);
    }
}

int main(void) {
    CcPlatform *platform = cc_platform_create("Indexed offscreen validation", 160, 90);
    if (!platform)
        return 77;
    CcIndexedRenderer *renderer = cc_indexed_create(platform, NULL, 0);
    PASS_REQUIRE(renderer);
    CcIndexedRenderer *other = cc_indexed_create(platform, NULL, 0);
    PASS_REQUIRE(other);
    pass_admission(renderer, other);
    shared_depth_admission(renderer, other);
    cc_indexed_destroy(other);
    CcIndexedFrame unprepared = {.depth_attachment = true};
    PASS_REQUIRE(!cc_indexed_begin_passes(renderer, &unprepared, NULL, 0));
    PASS_REQUIRE(!cc_indexed_begin_passes_wait(renderer, &unprepared, NULL, 0));
    /* Exercise CI's 1x framebuffer, Retina's 2x framebuffer, and a size whose
     * texture scaling does not land on the same source texel boundaries. */
    const int drawable_sizes[][2] = {{160, 90}, {320, 180}, {158, 94}};
    for (size_t index = 0; index < sizeof(drawable_sizes) / sizeof(drawable_sizes[0]);
         index++)
        test_drawable_size(platform, renderer, drawable_sizes[index][0],
                           drawable_sizes[index][1]);
    PASS_REQUIRE(cc_indexed_wait(renderer, NULL, 0));
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Indexed Metal RGBA8/half-float ordered GPU passes passed.");
    return EXIT_SUCCESS;
}
