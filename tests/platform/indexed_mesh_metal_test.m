#include "../support/indexed_mesh_fixture.h"

int main(void) {
    CcPlatform *platform = cc_platform_create("Dynamic mesh validation", 160, 90);
    if (!platform)
        return 77;
    CcIndexedRenderer *renderer = cc_indexed_create(platform, NULL, 0);
    CHECK(renderer);
    MeshFixture fixture;
    mesh_fixture_open(&fixture, renderer);
    CcIndexedRenderer *other = cc_indexed_create(platform, NULL, 0);
    CHECK(other);
    CHECK(!cc_indexed_mesh_update(other, fixture.mesh, fixture.vertices,
                                  sizeof(fixture.vertices), NULL, 0));
    cc_indexed_destroy(other);
    for (unsigned iteration = 0; iteration < 12; ++iteration) {
        uint8_t color[4] = {(uint8_t)(iteration * 13), 41, 83, 255};
        mesh_frame(&fixture, (iteration & 1) != 0, (iteration & 2) != 0, color);
    }
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        bool right = (iteration & 1) != 0;
        uint8_t color[4] = {37, (uint8_t)(iteration * 23), 113, 255};
        CcFramebuffer frame;
        CHECK(cc_platform_capture_begin(platform, &frame));
        mesh_frame(&fixture, (iteration & 2) != 0, right, color);
        CHECK(cc_platform_capture_frame(platform, &frame));
        CHECK(frame.rgba && frame.width > 0 && frame.height > 0);
        size_t y = (size_t)frame.height / 2;
        size_t x = (size_t)frame.width * (right ? 3u : 1u) / 4;
        mesh_pixels(frame.rgba + y * frame.stride + x * 4, color);
        const uint8_t black[4] = {0, 0, 0, 255};
        x = (size_t)frame.width * (right ? 1u : 3u) / 4;
        mesh_pixels(frame.rgba + y * frame.stride + x * 4, black);
        cc_platform_capture_end(platform);
    }
    const uint8_t final_color[4] = {83, 127, 191, 255};
    mesh_frame(&fixture, true, false, final_color);
    /* Submitted commands retain native buffers after application handles and
     * CPU staging retire, without an explicit application wait. */
    mesh_fixture_close(&fixture);
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Metal fixed-slot mesh updates, accepted-draw gate and pixels passed.");
    return EXIT_SUCCESS;
}
