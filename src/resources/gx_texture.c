#include "console_common/resources/gx_texture.h"
#include "console_common/support/endian.h"

#include <string.h>

typedef struct CcGxTileShape {
    int width;
    int height;
    int bytes;
} CcGxTileShape;

void cc_gx_rgb565(uint16_t value, CcGxColorExpansion expansion, uint8_t color[4]) {
    if (expansion == CC_GX_COLOR_SCALE) {
        color[0] = (uint8_t)((value >> 11) * 255 / 31);
        color[1] = (uint8_t)(((value >> 5) & 63u) * 255 / 63);
        color[2] = (uint8_t)((value & 31u) * 255 / 31);
        color[3] = 255;
        return;
    }
    unsigned red = value >> 11;
    unsigned green = (value >> 5) & 63u;
    unsigned blue = value & 31u;
    color[0] = (uint8_t)((red << 3) | (red >> 2));
    color[1] = (uint8_t)((green << 2) | (green >> 4));
    color[2] = (uint8_t)((blue << 3) | (blue >> 2));
    color[3] = 255;
}

void cc_gx_rgb5a3(uint16_t value, CcGxColorExpansion expansion, uint8_t color[4]) {
    if (expansion == CC_GX_COLOR_SCALE) {
        if (value & 0x8000u) {
            color[0] = (uint8_t)(((value >> 10) & 31u) * 255 / 31);
            color[1] = (uint8_t)(((value >> 5) & 31u) * 255 / 31);
            color[2] = (uint8_t)((value & 31u) * 255 / 31);
            color[3] = 255;
        } else {
            color[0] = (uint8_t)(((value >> 8) & 15u) * 17);
            color[1] = (uint8_t)(((value >> 4) & 15u) * 17);
            color[2] = (uint8_t)((value & 15u) * 17);
            color[3] = (uint8_t)((value >> 12) * 255 / 7);
        }
        return;
    }
    if ((value & 0x8000u) != 0) {
        unsigned red = (value >> 10) & 31u;
        unsigned green = (value >> 5) & 31u;
        unsigned blue = value & 31u;
        color[0] = (uint8_t)((red << 3) | (red >> 2));
        color[1] = (uint8_t)((green << 3) | (green >> 2));
        color[2] = (uint8_t)((blue << 3) | (blue >> 2));
        color[3] = 255;
    } else {
        unsigned alpha = (value >> 12) & 7u;
        color[0] = (uint8_t)(((value >> 8) & 15u) * 17u);
        color[1] = (uint8_t)(((value >> 4) & 15u) * 17u);
        color[2] = (uint8_t)((value & 15u) * 17u);
        color[3] = (uint8_t)((alpha << 5) | (alpha << 2) | (alpha >> 1));
    }
}

static bool cc_shape(uint32_t format, CcGxTileShape *shape) {
    switch (format) {
        case 0:
        case 8:
        case 14:
            *shape = (CcGxTileShape){8, 8, 32};
            return true;
        case 1:
        case 2:
        case 9:
            *shape = (CcGxTileShape){8, 4, 32};
            return true;
        case 3:
        case 4:
        case 5:
        case 10:
            *shape = (CcGxTileShape){4, 4, 32};
            return true;
        case 6:
            *shape = (CcGxTileShape){4, 4, 64};
            return true;
        default:
            return false;
    }
}

static bool cc_decode_pixel(const uint8_t *tile, uint32_t format, int x, int y,
                            int tile_width, const uint8_t *palette,
                            size_t palette_count, CcGxColorExpansion expansion,
                            uint8_t color[4]) {
    int index = y * tile_width + x;
    uint32_t value;
    size_t palette_index = 0;

    switch (format) {
        case 0:
        case 8:
            value = (tile[index / 2] >> ((index & 1) != 0 ? 0 : 4)) & 15u;
            if (format == 8) {
                palette_index = value;
                break;
            }
            memset(color, (int)(value * 17u), 4);
            return true;
        case 1:
            memset(color, tile[index], 4);
            return true;
        case 2:
            value = tile[index];
            color[0] = (uint8_t)((value & 15u) * 17u);
            color[1] = color[0];
            color[2] = color[0];
            color[3] = (uint8_t)((value >> 4) * 17u);
            return true;
        case 3:
            value = cc_read_be16(tile + index * 2);
            color[0] = (uint8_t)value;
            color[1] = color[0];
            color[2] = color[0];
            color[3] = (uint8_t)(value >> 8);
            return true;
        case 4:
            cc_gx_rgb565(cc_read_be16(tile + index * 2), expansion, color);
            return true;
        case 5:
            cc_gx_rgb5a3(cc_read_be16(tile + index * 2), expansion, color);
            return true;
        case 6:
            color[0] = tile[index * 2 + 1];
            color[1] = tile[32 + index * 2];
            color[2] = tile[33 + index * 2];
            color[3] = tile[index * 2];
            return true;
        case 9:
            palette_index = tile[index];
            break;
        case 10:
            palette_index = cc_read_be16(tile + index * 2) & 0x3fffu;
            break;
        case 14: {
            size_t subblock = (size_t)((y / 4 * 2 + x / 4) * 8);
            uint16_t c0 = cc_read_be16(tile + subblock);
            uint16_t c1 = cc_read_be16(tile + subblock + 2);
            uint8_t first[4];
            uint8_t second[4];
            cc_gx_rgb565(c0, expansion, first);
            cc_gx_rgb565(c1, expansion, second);
            unsigned selector =
                (tile[subblock + 4 + (size_t)(y % 4)] >> (6 - 2 * (x % 4))) & 3u;
            if (selector == 0 || selector == 1) {
                memcpy(color, selector == 0 ? first : second, 4);
                return true;
            }
            if (c0 > c1) {
                unsigned first_weight = selector == 2 ? 5u : 3u;
                unsigned second_weight = 8u - first_weight;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    color[channel] = (uint8_t)((first_weight * first[channel] +
                                                second_weight * second[channel]) >>
                                               3);
                }
            } else {
                for (unsigned channel = 0; channel < 3; ++channel)
                    color[channel] = (uint8_t)((first[channel] + second[channel]) / 2u);
            }
            color[3] = c0 <= c1 && selector == 3 ? 0 : 255;
            return true;
        }
        default:
            return false;
    }

    if (palette == NULL || palette_index >= palette_count) {
        return false;
    }
    memcpy(color, palette + palette_index * 4, 4);
    return true;
}

bool cc_gx_texture_size(uint32_t format, unsigned width, unsigned height,
                        size_t *encoded_bytes, size_t *rgba_bytes) {
    CcGxTileShape shape;
    if (!encoded_bytes || !rgba_bytes || !width || !height || !cc_shape(format, &shape))
        return false;
    size_t columns = ((size_t)width - 1) / (size_t)shape.width + 1;
    size_t rows = ((size_t)height - 1) / (size_t)shape.height + 1;
    size_t limit = SIZE_MAX / (size_t)shape.bytes;
    if (columns > limit / rows || (size_t)width > SIZE_MAX / 4 / height)
        return false;
    *encoded_bytes = columns * rows * (size_t)shape.bytes;
    *rgba_bytes = (size_t)width * height * 4;
    return true;
}

bool cc_gx_texture_decode(const CcGxTexture *texture, uint8_t *rgba,
                          size_t rgba_bytes) {
    size_t encoded_size;
    size_t output_size;
    CcGxTileShape shape;
    if (!texture || !texture->pixels || !rgba ||
        (texture->expansion != CC_GX_COLOR_REPLICATE &&
         texture->expansion != CC_GX_COLOR_SCALE) ||
        texture->palette_count > SIZE_MAX / 4 ||
        !cc_gx_texture_size(texture->format, texture->width, texture->height,
                            &encoded_size, &output_size) ||
        encoded_size > texture->pixel_bytes || output_size > rgba_bytes ||
        !cc_shape(texture->format, &shape))
        return false;
    size_t tile_index = 0;
    for (size_t by = 0; by < texture->height; by += (size_t)shape.height) {
        for (size_t bx = 0; bx < texture->width; bx += (size_t)shape.width) {
            const uint8_t *tile = texture->pixels + tile_index * (size_t)shape.bytes;
            ++tile_index;
            for (int y = 0; y < shape.height; ++y) {
                for (int x = 0; x < shape.width; ++x) {
                    if (bx + (size_t)x >= texture->width ||
                        by + (size_t)y >= texture->height)
                        continue;
                    uint8_t color[4];
                    if (!cc_decode_pixel(tile, texture->format, x, y, shape.width,
                                         texture->palette, texture->palette_count,
                                         texture->expansion, color))
                        return false;
                    size_t offset =
                        ((by + (size_t)y) * texture->width + bx + (size_t)x) * 4;
                    memcpy(rgba + offset, color, 4);
                }
            }
        }
    }
    return true;
}
