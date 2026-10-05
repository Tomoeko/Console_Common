#include "../support/indexed_texture_fixture.h"

int main(void) {
    CcPlatform *platform = cc_platform_create("Dynamic texture validation", 160, 90);
    if (!platform)
        return 77;
    CcIndexedRenderer *renderer = cc_indexed_create(platform, NULL, 0);
    CHECK(renderer);
    TextureFixture fixture;
    texture_fixture_open(&fixture, renderer);
    CcIndexedRenderer *other = cc_indexed_create(platform, NULL, 0);
    CHECK(other);
    CHECK(!cc_indexed_texture_update(other, fixture.texture, 0, fixture.levels, 64,
                                     NULL, 0));
    cc_indexed_destroy(other);
    for (unsigned iteration = 0; iteration < 12; ++iteration) {
        uint8_t color[4] = {(uint8_t)(iteration * 13), 41, 83, 255};
        texture_frame(&fixture, (iteration & 1) != 0, 0, color);
    }
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        uint8_t color[4] = {37, (uint8_t)(iteration * 23), 113, 255};
        CcFramebuffer frame;
        CHECK(cc_platform_capture_begin(platform, &frame));
        texture_frame(&fixture, (iteration & 1) != 0, iteration < 4 ? 0 : 2, color);
        CHECK(cc_platform_capture_frame(platform, &frame));
        texture_pixels(&frame, color);
        cc_platform_capture_end(platform);
    }
    const uint8_t final_color[4] = {83, 127, 191, 255};
    texture_frame(&fixture, true, 0, final_color);
    /* Native commands must keep the just-submitted GPU resource alive while
     * the application handle and CPU staging retire without an explicit wait. */
    texture_fixture_close(&fixture);
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Metal fixed-slot dynamic texture updates and accepted-draw gate passed.");
    return EXIT_SUCCESS;
}
