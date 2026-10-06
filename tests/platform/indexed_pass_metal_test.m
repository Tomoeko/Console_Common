#include "../support/indexed_shared_depth_fixture.h"

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
    PASS_REQUIRE(cc_indexed_prepare_drawable_depth(renderer, NULL, 0));
    CcFramebuffer shared_frame;
    PASS_REQUIRE(cc_platform_capture_begin(platform, &shared_frame));
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
    PASS_REQUIRE(cc_indexed_wait(renderer, NULL, 0));
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Indexed Metal RGBA8/half-float ordered GPU passes passed.");
    return EXIT_SUCCESS;
}
