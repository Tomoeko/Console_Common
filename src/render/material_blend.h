#ifndef CC_RENDER_MATERIAL_BLEND_H
#define CC_RENDER_MATERIAL_BLEND_H

#include "console_common/platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct CcMaterialBlend {
    bool enabled;
    uint8_t source;
    uint8_t destination;
} CcMaterialBlend;

/* Resolve the GX blend factors once, before either backend changes GPU state.
 * An unsupported factor rejects the draw on both backends. */
bool cc_material_blend_resolve(const CcMaterialQuad *quad, CcMaterialBlend *blend);

#endif
