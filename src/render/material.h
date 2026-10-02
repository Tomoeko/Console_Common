#ifndef CC_RENDER_MATERIAL_H
#define CC_RENDER_MATERIAL_H

#include "console_common/platform/platform.h"

enum { CC_RENDER_TEV_STAGES = 6 };

typedef enum CcTevSupport {
    CC_TEV_NONE,
    CC_TEV_SUPPORTED,
    CC_TEV_STAGE_LIMIT,
    CC_TEV_PRECISION_LIMIT,
    CC_TEV_INVALID_ENCODING
} CcTevSupport;

/* Keep material selection shared. ES 2.0 fragment mediump cannot represent
 * the packed 24-bit comparisons; Metal and fragment highp can. */
CcTevSupport cc_material_tev_support(const CcMaterialQuad *quad, bool fragment_highp);

#endif
