#ifndef CC_INDEXED_PARTIAL_TEXTURE_FIXTURE_H
#define CC_INDEXED_PARTIAL_TEXTURE_FIXTURE_H

#include "console_common/platform/indexed.h"

#include <string.h>

/* Diagnostic colors distinguish all retained levels. These are backend
 * admission/sampling inputs, not PS3 resources or a runtime substitution. */
enum { PARTIAL_MIPS_BYTES = 10912, PARTIAL_MIPS_LEVELS = 5 };

static const uint8_t mip_colors[PARTIAL_MIPS_LEVELS][4] = {
    {37, 59, 83, 255},   {113, 41, 173, 255}, {71, 149, 31, 255},
    {199, 61, 127, 255}, {83, 127, 191, 255},
};

static CcIndexedTextureDescription partial_texture_description(uint8_t *pixels) {
    CcIndexedTextureDescription description = {
        .level_count = PARTIAL_MIPS_LEVELS,
        .min_filter = CC_INDEXED_LINEAR,
        .mag_filter = CC_INDEXED_LINEAR,
        .mip_filter = CC_INDEXED_MIP_LINEAR,
        .wrap_s = CC_INDEXED_CLAMP,
        .wrap_t = CC_INDEXED_CLAMP,
        .max_lod = 4,
        .max_anisotropy = 1,
    };
    unsigned width = 64;
    unsigned height = 32;
    size_t offset = 0;
    for (unsigned level = 0; level < PARTIAL_MIPS_LEVELS; ++level) {
        const size_t size = (size_t)width * height * 4;
        description.levels[level] =
            (CcIndexedMip){pixels + offset, size, width, height};
        for (size_t pixel = 0; pixel < size; pixel += 4)
            memcpy(pixels + offset + pixel, mip_colors[level], 4);
        offset += size;
        width /= 2;
        height /= 2;
    }
    return description;
}

#endif
