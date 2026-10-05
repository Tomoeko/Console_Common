#ifndef CC_INDEXED_TEXTURE_FIXTURE_H
#define CC_INDEXED_TEXTURE_FIXTURE_H

#include "console_common/platform/indexed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            exit(EXIT_FAILURE);                                                        \
        }                                                                              \
    } while (0)

static const char vertex_glsl[] = "attribute vec4 position;\n"
                                  "attribute vec4 coordinates;\n"
                                  "uniform vec4 uv_scale;\n"
                                  "varying vec2 uv;\n"
                                  "void main() {\n"
                                  "    gl_Position = position;\n"
                                  "    uv = coordinates.xy * uv_scale.xy;\n"
                                  "}\n";

static const char fragment_glsl[] = "#ifdef GL_ES\n"
                                    "precision mediump float;\n"
                                    "#endif\n"
                                    "uniform sampler2D image;\n"
                                    "varying vec2 uv;\n"
                                    "void main() {\n"
                                    "    gl_FragColor = texture2D(image, uv);\n"
                                    "}\n";

static const char vertex_metal[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[attribute(0)]];\n"
    "    float4 coordinates [[attribute(1)]];\n"
    "};\n"
    "struct Output {\n"
    "    float4 position [[position]];\n"
    "    float2 uv;\n"
    "};\n"
    "vertex Output vertex_main(Input input [[stage_in]],\n"
    "    constant float4& uv_scale [[buffer(1)]]) {\n"
    "    Output output;\n"
    "    output.position = input.position;\n"
    "    output.uv = input.coordinates.xy * uv_scale.xy;\n"
    "    return output;\n"
    "}\n";

static const char fragment_metal[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[position]];\n"
    "    float2 uv;\n"
    "};\n"
    "fragment float4 fragment_main(Input input [[stage_in]],\n"
    "    texture2d<float> image [[texture(0)]],\n"
    "    sampler image_sampler [[sampler(0)]]) {\n"
    "    return image.sample(image_sampler, input.uv);\n"
    "}\n";

typedef struct TextureFixture {
    CcIndexedRenderer *renderer;
    CcIndexedProgram *program;
    CcIndexedMesh *mesh;
    CcIndexedTexture *texture;
    CcIndexedTexture *immutable;
    CcIndexedTarget *target;
    CcIndexedDraw draw;
    float scale[1][4];
    uint8_t levels[84];
} TextureFixture;

static void fill_color(uint8_t *bytes, size_t count, const uint8_t color[4]) {
    for (size_t index = 0; index < count; index += 4)
        memcpy(bytes + index, color, 4);
}

static void texture_fixture_open(TextureFixture *fixture, CcIndexedRenderer *renderer) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->renderer = renderer;
    CcIndexedProgramDescription program = {0};
    program.gles_vertex_source = vertex_glsl;
    program.gles_fragment_source = fragment_glsl;
    program.metal_vertex_source = vertex_metal;
    program.metal_fragment_source = fragment_metal;
    program.metal_vertex_entry = "vertex_main";
    program.metal_fragment_entry = "fragment_main";
    program.attributes[0] = (CcIndexedAttribute){"position", 0};
    program.attributes[1] = (CcIndexedAttribute){"coordinates", 16};
    program.attribute_count = 2;
    program.vertex_stride = 32;
    program.vertex_uniform_count = 1;
    program.vertex_uniforms[0] = "uv_scale";
    program.metal_vertex_uniform_buffer = 1;
    program.texture_uniforms[0] = "image";
    program.texture_count = 1;
    for (size_t lane = 0; lane < 4; ++lane)
        program.state.color_write[lane] = true;
    fixture->program = cc_indexed_program_create(renderer, &program, NULL, 0);
    CHECK(fixture->program);
    const float vertices[4][8] = {{-1, -1, 0, 1, 0, 0, 0, 0},
                                  {1, -1, 0, 1, 1, 0, 0, 0},
                                  {1, 1, 0, 1, 1, 1, 0, 0},
                                  {-1, 1, 0, 1, 0, 1, 0, 0}};
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    CcIndexedMeshDescription mesh = {vertices, 4, sizeof(vertices[0]), indices, 6};
    fixture->mesh = cc_indexed_mesh_create(renderer, &mesh, NULL, 0);
    CHECK(fixture->mesh);
    CcIndexedTextureDescription texture = {0};
    texture.levels[0] = (CcIndexedMip){fixture->levels, 64, 4, 4};
    texture.levels[1] = (CcIndexedMip){fixture->levels + 64, 16, 2, 2};
    texture.levels[2] = (CcIndexedMip){fixture->levels + 80, 4, 1, 1};
    texture.level_count = 3;
    texture.mip_filter = CC_INDEXED_MIP_NEAREST;
    texture.max_lod = 2;
    texture.max_anisotropy = 1;
    fixture->texture = cc_indexed_texture_create_dynamic(renderer, &texture, NULL, 0);
    CHECK(fixture->texture);
    fixture->immutable = cc_indexed_texture_create(renderer, &texture, NULL, 0);
    CHECK(fixture->immutable);
    CcIndexedTargetDescription target = {.width = 4, .height = 4};
    fixture->target = cc_indexed_target_create(renderer, &target, NULL, 0);
    CHECK(fixture->target);
    fixture->scale[0][0] = fixture->scale[0][1] = 1;
    fixture->draw =
        (CcIndexedDraw){.program = fixture->program,
                        .mesh = fixture->mesh,
                        .vertex_uniforms = (const float (*)[4])fixture->scale,
                        .vertex_uniform_count = 1,
                        .textures = {fixture->texture},
                        .texture_count = 1,
                        .index_count = 6};
    CHECK(cc_indexed_reserve(renderer, 2, NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, fixture->immutable, 0, fixture->levels,
                                     64, NULL, 0));
    CHECK(!cc_indexed_texture_update(
        renderer, cc_indexed_target_texture(renderer, fixture->target), 0,
        fixture->levels, 64, NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, (CcIndexedTexture *)(uintptr_t)8, 0,
                                     fixture->levels, 64, NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, fixture->texture, 3, fixture->levels, 64,
                                     NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, fixture->texture, 0, NULL, 64, NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, fixture->texture, 0, fixture->levels, 63,
                                     NULL, 0));
    CHECK(!cc_indexed_texture_update(renderer, fixture->texture, 0,
                                     (const uint8_t *)(UINTPTR_MAX - 31), 64, NULL, 0));
    texture.levels[0].rgba = (const uint8_t *)(UINTPTR_MAX - 31);
    CHECK(!cc_indexed_texture_create_dynamic(renderer, &texture, NULL, 0));
    CHECK(!cc_indexed_texture_create_dynamic(
        renderer, (const void *)((uintptr_t)&texture + 1), NULL, 0));
    CHECK(!cc_indexed_texture_create_dynamic(
        renderer, (const void *)(UINTPTR_MAX - sizeof(texture) + 1), NULL, 0));
}

static void texture_frame(TextureFixture *fixture, bool inside, unsigned level,
                          const uint8_t color[4]) {
    static const size_t sizes[3] = {64, 16, 4};
    uint8_t replacement[64];
    fill_color(replacement, sizes[level], color);
    if (!inside)
        CHECK(cc_indexed_texture_update(fixture->renderer, fixture->texture, level,
                                        replacement, sizes[level], NULL, 0));
    CcIndexedFrame frame = {.clear_color = {0, 0, 0, 1}, .clear_color_enabled = true};
    CHECK(cc_indexed_begin(fixture->renderer, &frame, NULL, 0));
    if (inside) {
        /* A rejected draw does not reserve this texture's contents. */
        fixture->draw.index_count = 5;
        CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
        fixture->draw.index_count = 6;
        CHECK(cc_indexed_texture_update(fixture->renderer, fixture->texture, level,
                                        replacement, sizes[level], NULL, 0));
    }
    fixture->scale[0][0] = fixture->scale[0][1] = level == 0 ? 1 : 256;
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    const uint8_t poison[4] = {255, 0, 255, 255};
    fill_color(replacement, sizes[level], poison);
    CHECK(!cc_indexed_texture_update(fixture->renderer, fixture->texture, level,
                                     replacement, sizes[level], NULL, 0));
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(cc_indexed_end(fixture->renderer, NULL, 0));
}

static void texture_pixels(const CcFramebuffer *frame, const uint8_t color[4]) {
    const uint8_t *pixel = frame->rgba + (size_t)(frame->height / 2) * frame->stride +
                           (size_t)(frame->width / 2) * 4;
    for (size_t lane = 0; lane < 4; ++lane) {
        int difference = (int)pixel[lane] - color[lane];
        CHECK(difference >= -1 && difference <= 1);
    }
}

static void texture_fixture_close(TextureFixture *fixture) {
    CHECK(cc_indexed_wait(fixture->renderer, NULL, 0));
    CHECK(cc_indexed_target_release(fixture->renderer, &fixture->target, NULL, 0));
    CHECK(cc_indexed_texture_release(fixture->renderer, &fixture->texture, NULL, 0));
    CHECK(cc_indexed_texture_release(fixture->renderer, &fixture->immutable, NULL, 0));
    CHECK(cc_indexed_mesh_release(fixture->renderer, &fixture->mesh, NULL, 0));
    CHECK(cc_indexed_program_release(fixture->renderer, &fixture->program, NULL, 0));
}

#endif
