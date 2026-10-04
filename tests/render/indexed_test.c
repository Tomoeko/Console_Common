#include "indexed.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void require(bool condition, const char *expression, int line) {
    if (!condition) {
        fprintf(stderr, "indexed_test.c:%d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define REQUIRE(condition) require((condition), #condition, __LINE__)

static CcIndexedProgramDescription program_description(void) {
    CcIndexedProgramDescription description = {0};
    description.attribute_count = 2;
    description.attributes[0] = (CcIndexedAttribute){"position", 0};
    description.attributes[1] = (CcIndexedAttribute){"color", 16};
    description.vertex_stride = 32;
    description.vertex_uniform_count = 1;
    description.vertex_uniforms[0] = "transform";
    description.fragment_uniform_count = 1;
    description.fragment_uniforms[0] = "tint";
    description.metal_vertex_uniform_buffer = 1;
    description.state.depth_compare = CC_INDEXED_LESS_EQUAL;
    return description;
}

static void program_test(void) {
    CcIndexedProgramDescription description = program_description();
    REQUIRE(cc_indexed_program_validate(&description, NULL, 0));
    description.attributes[1].offset = 20;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description.attributes[1].offset = 16;
    description.attributes[1].name = "position";
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description = program_description();
    description.state.cull = (CcIndexedCull)-1;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description = program_description();
    description.vertex_uniform_count = 2;
    description.vertex_uniforms[1] = "transform";
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description.vertex_uniforms[1] = "other";
    REQUIRE(cc_indexed_program_validate(&description, NULL, 0));
    description.fragment_uniforms[0] = "other";
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description.fragment_uniforms[0] = "tint";
    description.metal_vertex_uniform_buffer = 0;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description = program_description();
    description.state.source_rgb = CC_INDEXED_CONSTANT_COLOR;
    description.state.blend_color[0] = 0.25f;
    description.state.blend_color[1] = 0.5f;
    description.state.blend_color[2] = 0.75f;
    description.state.blend_color[3] = 1.0f;
    REQUIRE(cc_indexed_program_validate(&description, NULL, 0));
    description.state.blend_color[2] = NAN;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description.state.blend_color[2] = -0.25f;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description.state.blend_color[2] = 1.25f;
    REQUIRE(!cc_indexed_program_validate(&description, NULL, 0));
    description = program_description();
    description.fragment_uniform_count = CC_INDEXED_UNIFORMS + 1;
    char error[16];
    REQUIRE(!cc_indexed_program_validate(&description, error, sizeof(error)));
    REQUIRE(error[0] && error[sizeof(error) - 1] == '\0');
    REQUIRE(!cc_indexed_program_validate(NULL, NULL, 0));
}

static void mesh_test(void) {
    const float vertices[3][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {0, 1, 0, 1}};
    uint16_t indices[] = {0, 1, 2};
    CcIndexedMeshDescription description = {vertices, 3, sizeof(vertices[0]), indices,
                                            3};
    REQUIRE(cc_indexed_mesh_validate(&description, NULL, 0));
    indices[2] = 3;
    REQUIRE(!cc_indexed_mesh_validate(&description, NULL, 0));
    indices[2] = 2;
    description.index_count = 2;
    REQUIRE(!cc_indexed_mesh_validate(&description, NULL, 0));
    description.index_count = 3;
    description.vertex_count = UINT16_MAX + 2u;
    REQUIRE(!cc_indexed_mesh_validate(&description, NULL, 0));
    description.vertex_count = 3;
    description.vertex_stride = 17;
    REQUIRE(!cc_indexed_mesh_validate(&description, NULL, 0));
    description.vertex_stride = SIZE_MAX;
    REQUIRE(!cc_indexed_mesh_validate(&description, NULL, 0));
    REQUIRE(!cc_indexed_mesh_validate(NULL, NULL, 0));
}

static void texture_test(void) {
    const uint8_t rgba[64] = {0};
    CcIndexedTextureDescription description = {0};
    description.max_anisotropy = 1;
    description.max_lod = 13.0f;
    description.level_count = 3;
    description.levels[0] = (CcIndexedMip){rgba, 64, 4, 4};
    description.levels[1] = (CcIndexedMip){rgba, 16, 2, 2};
    description.levels[2] = (CcIndexedMip){rgba, 4, 1, 1};
    description.mip_filter = CC_INDEXED_MIP_LINEAR;
    description.wrap_s = CC_INDEXED_REPEAT;
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.level_count = 2;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.mip_filter = CC_INDEXED_MIP_NONE;
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.levels[1].size = 15;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.level_count = 1;
    description.levels[0] = (CcIndexedMip){rgba, 24, 3, 2};
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.wrap_s = CC_INDEXED_CLAMP;
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.mip_filter = CC_INDEXED_MIP_NEAREST;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.level_count = 2;
    description.levels[1] = (CcIndexedMip){rgba, 4, 1, 1};
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.max_anisotropy = 0;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.max_anisotropy = 17;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.max_anisotropy = 4;
    REQUIRE(cc_indexed_texture_validate(&description, NULL, 0));
    description.min_lod = -1.0f;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.min_lod = 14.0f;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.min_lod = NAN;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.min_lod = 0.0f;
    description.max_lod = INFINITY;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    description.max_lod = 13.0f;
    description.mip_filter = CC_INDEXED_MIP_NONE;
    description.levels[0].width = UINT_MAX;
    REQUIRE(!cc_indexed_texture_validate(&description, NULL, 0));
    REQUIRE(!cc_indexed_texture_validate(NULL, NULL, 0));
}

static void frame_uniform_test(void) {
    CcIndexedFrame frame = {.clear_depth = 1.0f, .clear_depth_enabled = true};
    REQUIRE(!cc_indexed_frame_validate(&frame, NULL, 0));
    frame.depth_attachment = true;
    REQUIRE(cc_indexed_frame_validate(&frame, NULL, 0));
    frame.clear_color.r = INFINITY;
    REQUIRE(!cc_indexed_frame_validate(&frame, NULL, 0));
    frame.clear_color.r = 0.0f;
    frame.clear_depth = -1.0f;
    REQUIRE(!cc_indexed_frame_validate(&frame, NULL, 0));
    const float valid[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    const float invalid[1][4] = {{0, 0, NAN, 0}};
    REQUIRE(cc_indexed_uniforms_validate(valid, 2, 2, NULL, 0));
    REQUIRE(!cc_indexed_uniforms_validate(valid, 2, 1, NULL, 0));
    REQUIRE(!cc_indexed_uniforms_validate(invalid, 1, 1, NULL, 0));
    REQUIRE(cc_indexed_uniforms_validate(NULL, 0, 0, NULL, 0));
    REQUIRE(!cc_indexed_uniforms_validate(NULL, 1, 1, NULL, 0));
}

int main(void) {
    program_test();
    mesh_test();
    texture_test();
    frame_uniform_test();
    puts("Indexed validation tests passed.");
    return EXIT_SUCCESS;
}
