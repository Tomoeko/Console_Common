#ifndef CONSOLE_COMMON_GX_TEXTURE_H
#define CONSOLE_COMMON_GX_TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CC_GX_COLOR_REPLICATE, /* Extend low-bit colors by repeating their high bits. */
    CC_GX_COLOR_SCALE      /* Floor the channel's fraction of 255. */
} CcGxColorExpansion;

/* Encoded tiles and optional RGBA palette are borrowed. The palette contains
 * palette_count four-byte colors. Wrappers retain their own format limits and
 * choose the existing color expansion policy explicitly. */
typedef struct {
    unsigned width;
    unsigned height;
    uint32_t format;
    const uint8_t *pixels;
    size_t pixel_bytes;
    const uint8_t *palette;
    size_t palette_count;
    CcGxColorExpansion expansion;
} CcGxTexture;

bool cc_gx_texture_size(uint32_t format, unsigned width, unsigned height,
                        size_t *encoded_bytes, size_t *rgba_bytes);
/* The output is borrowed, must not overlap encoded input, and remains owned
 * by the caller. No allocation occurs; a bad palette index can leave a
 * partially decoded output, so loaders commit ownership only on success. */
bool cc_gx_texture_decode(const CcGxTexture *texture, uint8_t *rgba, size_t rgba_bytes);
void cc_gx_rgb565(uint16_t value, CcGxColorExpansion expansion, uint8_t color[4]);
void cc_gx_rgb5a3(uint16_t value, CcGxColorExpansion expansion, uint8_t color[4]);

#endif
