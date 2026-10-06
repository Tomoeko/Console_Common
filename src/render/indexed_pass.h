#ifndef CC_RENDER_INDEXED_PASS_H
#define CC_RENDER_INDEXED_PASS_H

#include "console_common/platform/indexed.h"

typedef struct CcIndexedPassRecord {
    CcIndexedPass description;
    size_t first_draw;
    size_t draw_count;
    bool color_valid;
    bool depth_valid;
    CcIndexedTarget *depth_owner;
} CcIndexedPassRecord;

static inline CcViewport cc_indexed_pass_scissor(const CcIndexedPass *pass) {
    return pass->scissor_enabled ? pass->scissor : pass->viewport;
}

/* Metal's native scissor must fit the attachment. Keep this backend projection
 * separate from the requested rectangle retained in the command record. */
static inline CcViewport cc_indexed_pass_scissor_clip(const CcIndexedPass *pass,
                                                      unsigned width, unsigned height) {
    CcViewport rectangle = cc_indexed_pass_scissor(pass);
    unsigned x = (unsigned)rectangle.x;
    unsigned y = (unsigned)rectangle.y;
    if (x > width)
        x = width;
    if (y > height)
        y = height;
    unsigned clipped_width = (unsigned)rectangle.width;
    unsigned clipped_height = (unsigned)rectangle.height;
    if (clipped_width > width - x)
        clipped_width = width - x;
    if (clipped_height > height - y)
        clipped_height = height - y;
    return (CcViewport){(int)x, (int)y, (int)clipped_width, (int)clipped_height};
}

/* Records are transient ordered commands, not another texture payload. */
static inline void cc_indexed_pass_content(const CcIndexedPassRecord *passes,
                                           size_t count, CcIndexedTarget *target,
                                           CcIndexedTarget *depth_owner,
                                           bool *color_valid, bool *depth_valid) {
    for (size_t index = 0; index < count; ++index) {
        if (passes[index].description.target == target)
            *color_valid = passes[index].color_valid;
        if (depth_owner && passes[index].depth_owner == depth_owner &&
            passes[index].description.depth_attachment)
            *depth_valid = passes[index].depth_valid;
    }
}

static inline CcIndexedPassRecord cc_indexed_pass_record(const CcIndexedPass *pass,
                                                         size_t first_draw,
                                                         CcIndexedTarget *depth_owner,
                                                         bool color_valid,
                                                         bool depth_valid) {
    if (pass->color_load != CC_INDEXED_LOAD)
        color_valid = pass->color_load == CC_INDEXED_CLEAR || pass->color_full_write;
    if (pass->depth_attachment && pass->depth_load != CC_INDEXED_LOAD)
        depth_valid = pass->depth_load == CC_INDEXED_CLEAR || pass->depth_full_write;
    return (CcIndexedPassRecord){.description = *pass,
                                 .first_draw = first_draw,
                                 .color_valid = color_valid,
                                 .depth_valid = depth_valid,
                                 .depth_owner = depth_owner};
}

#endif
