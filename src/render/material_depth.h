#ifndef CONSOLE_COMMON_MATERIAL_DEPTH_H
#define CONSOLE_COMMON_MATERIAL_DEPTH_H

#include "console_common/platform/platform.h"

#include <math.h>

/* Zero is the compatible 2D state; the other sixteen keys combine one GX
 * comparison with a write bit. Backends cache this small, fixed state set. */
static inline bool cc_material_depth_key(const CcMaterialQuad *quad, unsigned *key) {
    if (!quad || !key)
        return false;
    *key = 0;
    if (quad->has_depth_mode && quad->depth_mode[0]) {
        if (quad->depth_mode[1] > 7)
            return false;
        *key = 1u + (unsigned)quad->depth_mode[1] * 2u + !!quad->depth_mode[2];
    }
    for (unsigned index = 0; index < 4; ++index) {
        const CcMaterialVertex *vertex = &quad->vertices[index];
        if (!isfinite(vertex->depth) || !isfinite(vertex->clip_w) || vertex->clip_w < 0)
            return false;
    }
    return true;
}

static inline float cc_material_clip_w(const CcMaterialVertex *vertex) {
    return vertex->clip_w == 0 ? 1 : vertex->clip_w;
}

#endif
