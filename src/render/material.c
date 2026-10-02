#include "material.h"

CcTevSupport cc_material_tev_support(const CcMaterialQuad *quad, bool fragment_highp) {
    if (!quad || quad->texture_count > CC_MATERIAL_TEXTURES) {
        return CC_TEV_INVALID_ENCODING;
    }
    if (quad->tev_stage_count == 0) {
        return CC_TEV_NONE;
    }
    if (quad->tev_stage_count > CC_RENDER_TEV_STAGES) {
        return CC_TEV_STAGE_LIMIT;
    }
    if (!fragment_highp) {
        for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
            unsigned operation = quad->tev_stages[stage][6] & 15;
            if (operation == 12 || operation == 13) {
                return CC_TEV_PRECISION_LIMIT;
            }
        }
    }
    if (quad->has_alpha_compare &&
        ((quad->alpha_compare[0] & 15) > 7 || (quad->alpha_compare[0] >> 4) > 7 ||
         quad->alpha_compare[1] > 3)) {
        return CC_TEV_INVALID_ENCODING;
    }
    for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
        const uint8_t *bytes = quad->tev_stages[stage];
        if ((bytes[8] & 15) > 7 || (bytes[8] >> 4) > 7 || (bytes[9] & 15) > 7 ||
            (bytes[9] >> 4) > 7) {
            return CC_TEV_INVALID_ENCODING;
        }
    }
    return CC_TEV_SUPPORTED;
}
