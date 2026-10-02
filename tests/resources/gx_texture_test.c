#include "console_common/resources/gx_texture.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void test_color_policies(void) {
    uint8_t color[4];
    cc_gx_rgb565(0x2084, CC_GX_COLOR_REPLICATE, color);
    const uint8_t replicate[] = {33, 16, 33, 255};
    assert(!memcmp(color, replicate, 4));
    cc_gx_rgb565(0x2084, CC_GX_COLOR_SCALE, color);
    const uint8_t scale[] = {32, 16, 32, 255};
    assert(!memcmp(color, scale, 4));
    cc_gx_rgb5a3(0x2123, CC_GX_COLOR_REPLICATE, color);
    const uint8_t replicate_alpha[] = {17, 34, 51, 73};
    assert(!memcmp(color, replicate_alpha, 4));
    cc_gx_rgb5a3(0x2123, CC_GX_COLOR_SCALE, color);
    const uint8_t scale_alpha[] = {17, 34, 51, 72};
    assert(!memcmp(color, scale_alpha, 4));
}

static void test_tiles_and_bounds(void) {
    uint8_t tiles[64] = {0};
    for (unsigned index = 0; index < 16; ++index) {
        tiles[index * 2] = (uint8_t)(index + 1);
        tiles[index * 2 + 1] = (uint8_t)(index + 21);
        tiles[index * 2 + 32] = (uint8_t)(index + 41);
        tiles[index * 2 + 33] = (uint8_t)(index + 61);
    }
    CcGxTexture texture = {.width = 3,
                           .height = 2,
                           .format = 6,
                           .pixels = tiles,
                           .pixel_bytes = sizeof(tiles)};
    uint8_t output[24];
    assert(cc_gx_texture_decode(&texture, output, sizeof(output)));
    const uint8_t expected[] = {21, 41, 61, 1, 22, 42, 62, 2, 23, 43, 63, 3,
                                25, 45, 65, 5, 26, 46, 66, 6, 27, 47, 67, 7};
    assert(!memcmp(output, expected, sizeof(output)));
    memset(output, 0xa5, sizeof(output));
    texture.pixel_bytes = 63;
    assert(!cc_gx_texture_decode(&texture, output, sizeof(output)));
    for (size_t index = 0; index < sizeof(output); ++index)
        assert(output[index] == 0xa5);
    texture.pixel_bytes = sizeof(tiles);
    assert(!cc_gx_texture_decode(&texture, output, sizeof(output) - 1));
    texture.expansion = (CcGxColorExpansion)2;
    assert(!cc_gx_texture_decode(&texture, output, sizeof(output)));
    size_t encoded;
    size_t rgba;
    assert(!cc_gx_texture_size(7, 4, 4, &encoded, &rgba));
    assert(!cc_gx_texture_size(6, 0, 4, &encoded, &rgba));
    assert(!cc_gx_texture_size(6, UINT_MAX, UINT_MAX, &encoded, &rgba));
    assert(cc_gx_texture_size(6, 3, 2, &encoded, &rgba));
    assert(encoded == 64 && rgba == sizeof(output));
}

static void test_palettes_and_compression(void) {
    uint8_t tiles[32] = {1};
    const uint8_t palette[] = {1, 2, 3, 4, 11, 12, 13, 14};
    CcGxTexture texture = {.width = 1,
                           .height = 1,
                           .format = 9,
                           .pixels = tiles,
                           .pixel_bytes = sizeof(tiles),
                           .palette = palette,
                           .palette_count = 2};
    uint8_t output[16];
    assert(cc_gx_texture_decode(&texture, output, 4));
    assert(!memcmp(output, palette + 4, 4));
    texture.palette_count = 1;
    assert(!cc_gx_texture_decode(&texture, output, 4));
    texture.palette = NULL;
    assert(!cc_gx_texture_decode(&texture, output, 4));
    texture.format = 14;
    texture.width = 4;
    texture.palette_count = 0;
    memset(tiles, 0, sizeof(tiles));
    tiles[0] = tiles[1] = 255;
    tiles[4] = 0x1b;
    assert(cc_gx_texture_decode(&texture, output, sizeof(output)));
    const uint8_t expected[] = {255, 255, 255, 255, 0,  0,  0,  255,
                                159, 159, 159, 255, 95, 95, 95, 255};
    assert(!memcmp(output, expected, sizeof(output)));
    tiles[0] = tiles[1] = 0;
    tiles[2] = tiles[3] = 255;
    assert(cc_gx_texture_decode(&texture, output, sizeof(output)));
    assert(output[15] == 0);
}

int main(void) {
    test_color_policies();
    test_tiles_and_bounds();
    test_palettes_and_compression();
    return 0;
}
