#include "material.h"
#include "frame_damage.h"

#include <assert.h>
#include <string.h>

static CcMaterialQuad valid_material(void) {
    CcMaterialQuad quad = {0};
    quad.tev_stage_count = CC_RENDER_TEV_STAGES;
    quad.texture_count = CC_MATERIAL_TEXTURES;
    quad.has_alpha_compare = true;
    quad.alpha_compare[0] = 0x77;
    quad.alpha_compare[1] = 3;
    for (unsigned stage = 0; stage < quad.tev_stage_count; ++stage) {
        quad.tev_stages[stage][8] = 0x77;
        quad.tev_stages[stage][9] = 0x77;
    }
    return quad;
}

static void test_bounds_and_selectors(void) {
    CcMaterialQuad quad = valid_material();
    assert(cc_material_tev_support(&quad, true) == CC_TEV_SUPPORTED);
    assert(cc_material_tev_support(&quad, false) == CC_TEV_SUPPORTED);
    for (unsigned byte = 8; byte <= 9; ++byte) {
        for (unsigned half = 0; half < 2; ++half) {
            quad.tev_stages[5][byte] = half ? 0x87 : 0x78;
            assert(cc_material_tev_support(&quad, true) == CC_TEV_INVALID_ENCODING);
            quad = valid_material();
        }
    }
    quad.alpha_compare[0] = 0x78;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_INVALID_ENCODING);
    quad.alpha_compare[0] = 0x87;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_INVALID_ENCODING);
    quad.alpha_compare[0] = 0x77;
    quad.alpha_compare[1] = 4;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_INVALID_ENCODING);
    quad.has_alpha_compare = false;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_SUPPORTED);
    quad.texture_count = CC_MATERIAL_TEXTURES + 1;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_INVALID_ENCODING);
    assert(cc_material_tev_support(NULL, true) == CC_TEV_INVALID_ENCODING);

    quad = valid_material();
    quad.tev_stage_count = CC_RENDER_TEV_STAGES + 1;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_STAGE_LIMIT);
    quad.tev_stage_count = 0;
    assert(cc_material_tev_support(&quad, true) == CC_TEV_NONE);
}

static void test_fragment_precision(void) {
    CcMaterialQuad quad = valid_material();
    for (unsigned operation = 0; operation < 16; ++operation) {
        quad.tev_stages[5][6] = (uint8_t)operation;
        CcMaterialQuad before = quad;
        assert(cc_material_tev_support(&quad, true) == CC_TEV_SUPPORTED);
        CcTevSupport expected = operation == 12 || operation == 13
                                    ? CC_TEV_PRECISION_LIMIT
                                    : CC_TEV_SUPPORTED;
        assert(cc_material_tev_support(&quad, false) == expected);
        assert(memcmp(&quad, &before, sizeof(quad)) == 0);
    }
}

static void test_retained_sampling(void) {
    CcFrameDamage *damage = cc_frame_damage_create();
    assert(damage);
    CcMaterialQuad quad = {0};
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
        quad.vertices[vertex].x = vertex & 1 ? 4 : 0;
        quad.vertices[vertex].y = vertex & 2 ? 4 : 0;
    }
    for (unsigned frame = 0; frame < 3; ++frame) {
        cc_frame_damage_begin(damage, 8, 8, (CcColor){0, 0, 0, 1});
        quad.nearest[2] = frame != 0;
        assert(cc_frame_damage_material(damage, &quad, NULL));
        size_t count = 0;
        const CcFrameCommand *commands = cc_frame_damage_commands(damage, &count);
        assert(count == 1 && commands[0].draw.material.nearest[2] == quad.nearest[2]);
        (void)cc_frame_damage_regions(damage, &count);
        assert((count != 0) == (frame < 2));
        cc_frame_damage_commit(damage);
    }
    cc_frame_damage_destroy(damage);
}

int main(void) {
    test_bounds_and_selectors();
    test_fragment_precision();
    test_retained_sampling();
    return 0;
}
