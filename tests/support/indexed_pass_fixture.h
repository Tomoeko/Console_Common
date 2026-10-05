#ifndef CC_TEST_INDEXED_PASS_FIXTURE_H
#define CC_TEST_INDEXED_PASS_FIXTURE_H

#include "console_common/platform/indexed.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void pass_require(bool condition, const char *expression, int line) {
    if (!condition) {
        fprintf(stderr, "indexed_pass_fixture.h:%d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define PASS_REQUIRE(condition) pass_require((condition), #condition, __LINE__)

static CcIndexedProgram *pass_program(CcIndexedRenderer *renderer) {
    static const char metal_vertex[] =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct Input {\n"
        "    float4 position [[attribute(0)]];\n"
        "    float4 uv [[attribute(1)]];\n"
        "};\n"
        "struct Output {\n"
        "    float4 position [[position]];\n"
        "    float2 uv;\n"
        "};\n"
        "vertex Output vertex_main(Input input [[stage_in]]) {\n"
        "    Output output;\n"
        "    output.position = input.position;\n"
        "    output.uv = input.uv.xy;\n"
        "    return output;\n"
        "}\n";
    static const char metal_fragment[] =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct Input {\n"
        "    float4 position [[position]];\n"
        "    float2 uv;\n"
        "};\n"
        "fragment float4 fragment_main(Input input [[stage_in]],\n"
        "    constant float4& tint [[buffer(2)]],\n"
        "    texture2d<float> image [[texture(0)]],\n"
        "    sampler image_sampler [[sampler(0)]]) {\n"
        "    return tint * image.sample(image_sampler, input.uv);\n"
        "}\n";
    static const char gles_vertex[] = "attribute vec4 position;\n"
                                      "attribute vec4 uv;\n"
                                      "varying vec2 texture_uv;\n"
                                      "void main() {\n"
                                      "    gl_Position = position;\n"
                                      "    texture_uv = vec2(uv.x, 1.0 - uv.y);\n"
                                      "}\n";
    static const char gles_fragment[] =
        "#ifdef GL_ES\n"
        "precision highp float;\n"
        "#endif\n"
        "varying vec2 texture_uv;\n"
        "uniform vec4 tint;\n"
        "uniform sampler2D image;\n"
        "void main() {\n"
        "    gl_FragColor = tint * texture2D(image, texture_uv);\n"
        "}\n";
    CcIndexedProgramDescription description = {
        .gles_vertex_source = gles_vertex,
        .gles_fragment_source = gles_fragment,
        .metal_vertex_source = metal_vertex,
        .metal_fragment_source = metal_fragment,
        .metal_vertex_entry = "vertex_main",
        .metal_fragment_entry = "fragment_main",
        .attributes = {{"position", 0}, {"uv", 16}},
        .attribute_count = 2,
        .vertex_stride = 32,
        .fragment_uniforms = {"tint"},
        .fragment_uniform_count = 1,
        .texture_uniforms = {"image"},
        .texture_count = 1,
        .metal_vertex_uniform_buffer = 1,
        .metal_fragment_uniform_buffer = 2,
        .state = {.color_write = {true, true, true, true},
                  .depth_test = true,
                  .depth_write = true,
                  .depth_compare = CC_INDEXED_LESS}};
    char error[256];
    CcIndexedProgram *program =
        cc_indexed_program_create(renderer, &description, error, sizeof(error));
    if (!program)
        fprintf(stderr, "%s\n", error);
    PASS_REQUIRE(program);
    return program;
}

static CcIndexedTarget *pass_target_size(CcIndexedRenderer *renderer,
                                         CcIndexedColorFormat format, bool depth,
                                         unsigned width, unsigned height) {
    CcIndexedTargetDescription description = {.width = width,
                                              .height = height,
                                              .color_format = format,
                                              .depth_attachment = depth,
                                              .min_filter = CC_INDEXED_NEAREST,
                                              .mag_filter = CC_INDEXED_NEAREST};
    CcIndexedTarget *target = cc_indexed_target_create(renderer, &description, NULL, 0);
    PASS_REQUIRE(target);
    return target;
}

static CcIndexedTarget *pass_target(CcIndexedRenderer *renderer,
                                    CcIndexedColorFormat format, bool depth) {
    return pass_target_size(renderer, format, depth, 32, 16);
}

static CcIndexedPass pass_description(CcIndexedTarget *target, CcIndexedLoad load) {
    return (CcIndexedPass){.target = target,
                           .viewport = {0, 0, 32, 16},
                           .color_load = load,
                           .depth_load = CC_INDEXED_DISCARD,
                           .clear_depth = 1.0f};
}

static void pass_admission(CcIndexedRenderer *renderer, CcIndexedRenderer *other) {
    CcIndexedTarget *target = pass_target(renderer, CC_INDEXED_RGBA8, false);
    CcIndexedTarget *foreign = pass_target(other, CC_INDEXED_RGBA8, false);
    PASS_REQUIRE(!cc_indexed_target_release(other, &target, NULL, 0));
    PASS_REQUIRE(!cc_indexed_target_texture(other, target));
    PASS_REQUIRE(cc_indexed_reserve_passes(renderer, 2, NULL, 0));
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    CcIndexedPass pass = pass_description(foreign, CC_INDEXED_CLEAR);
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = (CcIndexedTarget *)(uintptr_t)1;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass = pass_description(target, CC_INDEXED_DISCARD);
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(!cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    pass.color_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.color_load = CC_INDEXED_DISCARD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.color_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass = (CcIndexedPass){.viewport = {0, 0, 1, 1},
                           .color_load = CC_INDEXED_CLEAR,
                           .depth_load = CC_INDEXED_DISCARD};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &target, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(other, &foreign, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &target, NULL, 0));
}

static CcIndexedMesh *pass_mesh(CcIndexedRenderer *renderer) {
    const float vertices[4][8] = {{-1, 1, 0, 1, 0, 0, 0, 0},
                                  {1, 1, 0, 1, 1, 0, 0, 0},
                                  {-1, -1, 0, 1, 0, 1, 0, 0},
                                  {1, -1, 0, 1, 1, 1, 0, 0}};
    const uint16_t indices[] = {0, 2, 1, 1, 2, 3};
    CcIndexedMeshDescription mesh_description = {vertices, 4, sizeof(vertices[0]),
                                                 indices, 6};
    CcIndexedMesh *mesh = cc_indexed_mesh_create(renderer, &mesh_description, NULL, 0);
    PASS_REQUIRE(mesh);
    return mesh;
}

static CcIndexedTexture *pass_white(CcIndexedRenderer *renderer) {
    const uint8_t white[] = {255, 255, 255, 255};
    CcIndexedTextureDescription texture_description = {.levels = {{white, 4, 1, 1}},
                                                       .level_count = 1,
                                                       .max_lod = 13.0f,
                                                       .max_anisotropy = 1};
    CcIndexedTexture *texture =
        cc_indexed_texture_create(renderer, &texture_description, NULL, 0);
    PASS_REQUIRE(texture);
    return texture;
}

static void pass_scene(CcIndexedRenderer *renderer, int width, int height, bool hdr) {
    PASS_REQUIRE(cc_indexed_reserve(renderer, 8, NULL, 0));
    PASS_REQUIRE(cc_indexed_reserve_passes(renderer, 6, NULL, 0));
    CcIndexedProgram *program = pass_program(renderer);
    CcIndexedMesh *mesh = pass_mesh(renderer);
    CcIndexedTexture *texture = pass_white(renderer);
    CcIndexedColorFormat format = hdr ? CC_INDEXED_RGBA16_FLOAT : CC_INDEXED_RGBA8;
    CcIndexedTarget *source = pass_target(renderer, format, true);
    CcIndexedTarget *intermediate = pass_target(renderer, format, false);
    CcIndexedTarget *display = pass_target(renderer, CC_INDEXED_RGBA8, false);
    CcIndexedTexture *borrowed = cc_indexed_target_texture(renderer, source);
    PASS_REQUIRE(borrowed);
    PASS_REQUIRE(!cc_indexed_texture_release(renderer, &borrowed, NULL, 0));
    PASS_REQUIRE(borrowed == cc_indexed_target_texture(renderer, source));
    CcIndexedFrame frame = {.depth_attachment = hdr};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    float tint[1][4] = {{hdr ? 4.0f : 1.0f, 0.0f, 0.0f, 1.0f}};
    CcIndexedDraw draw = {.program = program,
                          .mesh = mesh,
                          .fragment_uniforms = (const float(*)[4])tint,
                          .fragment_uniform_count = 1,
                          .textures = {texture},
                          .texture_count = 1,
                          .index_count = 6};
    PASS_REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
    CcIndexedPass pass = pass_description(source, CC_INDEXED_LOAD);
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.color_load = CC_INDEXED_DISCARD;
    pass.color_full_write = true;
    pass.depth_attachment = true;
    pass.depth_load = CC_INDEXED_CLEAR;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(!cc_indexed_reserve_passes(renderer, 10, NULL, 0));
    PASS_REQUIRE(!cc_indexed_target_release(renderer, &source, NULL, 0));
    CcIndexedTargetDescription active_target = {.width = 8, .height = 8};
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &active_target, NULL, 0));
    PASS_REQUIRE(!cc_indexed_prepare_drawable_depth(renderer, NULL, 0));
    draw.textures[0] = borrowed;
    PASS_REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, intermediate);
    PASS_REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
    draw.textures[0] = texture;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass.color_load = CC_INDEXED_LOAD;
    pass.depth_load = CC_INDEXED_DISCARD;
    pass.depth_attachment = false;
    pass.color_full_write = false;
    pass.viewport = (CcViewport){0, 0, 16, 8};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    tint[0][0] = 0.0f;
    tint[0][1] = hdr ? 4.0f : 1.0f;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass = pass_description(intermediate, CC_INDEXED_DISCARD);
    pass.color_full_write = true;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    draw.textures[0] = borrowed;
    tint[0][0] = tint[0][1] = tint[0][2] = tint[0][3] = 1.0f;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass = pass_description(display, CC_INDEXED_CLEAR);
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, intermediate);
    tint[0][0] = tint[0][1] = tint[0][2] = hdr ? 0.25f : 1.0f;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass = (CcIndexedPass){
        .viewport = {width / 8, height / 8, width * 3 / 4, height * 3 / 4},
        .color_load = CC_INDEXED_CLEAR,
        .depth_load = CC_INDEXED_DISCARD,
        .clear_color = {0.2f, 0.4f, 0.6f, 1.0f}};
    pass.depth_attachment = hdr;
    pass.depth_load = hdr ? CC_INDEXED_CLEAR : CC_INDEXED_DISCARD;
    pass.clear_depth = 1.0f;
    CcIndexedPass bad = pass;
    bad.viewport.width = width + 1;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &bad, NULL, 0));
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, display);
    tint[0][0] = tint[0][1] = tint[0][2] = tint[0][3] = 1.0f;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_program_release(renderer, &program, NULL, 0));
    PASS_REQUIRE(cc_indexed_mesh_release(renderer, &mesh, NULL, 0));
    PASS_REQUIRE(cc_indexed_texture_release(renderer, &texture, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &source, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &intermediate, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &display, NULL, 0));
    PASS_REQUIRE(!cc_indexed_target_texture(renderer, source));
}

static void pass_pixel(const CcFramebuffer *frame, int x, int y,
                       const uint8_t expected[4]) {
    const uint8_t *pixel = frame->rgba + (size_t)y * frame->stride + (size_t)x * 4;
    for (size_t lane = 0; lane < 4; ++lane) {
        int difference = (int)pixel[lane] - expected[lane];
        if (difference < -1 || difference > 1)
            fprintf(stderr, "pixel %d,%d lane%zu: %u vs %u\n", x, y, lane, pixel[lane],
                    expected[lane]);
        PASS_REQUIRE(difference >= -1 && difference <= 1);
    }
}

static void pass_pixels(const CcFramebuffer *frame) {
    const uint8_t red[] = {255, 0, 0, 255};
    const uint8_t green[] = {0, 255, 0, 255};
    const uint8_t clear[] = {51, 102, 153, 255};
    pass_pixel(frame, 0, 0, clear);
    pass_pixel(frame, frame->width / 4, frame->height / 4, green);
    pass_pixel(frame, frame->width / 4, frame->height * 3 / 4, red);
    pass_pixel(frame, frame->width * 3 / 4, frame->height / 4, red);
}

static CcIndexedPass pass_rectangle(CcIndexedTarget *target, CcIndexedLoad load,
                                    CcViewport viewport, CcViewport scissor) {
    return (CcIndexedPass){.target = target,
                           .viewport = viewport,
                           .color_load = load,
                           .depth_load = CC_INDEXED_DISCARD,
                           .scissor_enabled = true,
                           .scissor = scissor};
}

static void pass_stripes(CcIndexedRenderer *renderer, CcIndexedTarget *target,
                         CcIndexedDraw *draw, float tint[1][4]) {
    static const float colors[4][4] = {
        {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}};
    for (int stripe = 0; stripe < 4; ++stripe) {
        CcIndexedPass pass = pass_rectangle(
            target, stripe ? CC_INDEXED_LOAD : CC_INDEXED_CLEAR,
            (CcViewport){0, stripe * 8, 32, 8}, (CcViewport){0, stripe * 8, 32, 8});
        PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
        for (size_t lane = 0; lane < 4; ++lane)
            tint[0][lane] = colors[stripe][lane];
        PASS_REQUIRE(cc_indexed_draw(renderer, draw, NULL, 0));
    }
}

/* These attachment/rectangle sizes exercise the recovered 720p and 576-line
 * Wave request shapes. This is a backend contract test, not a native image. */
static void pass_extended_scene(CcIndexedRenderer *renderer, int drawable_width,
                                int drawable_height, int output_width,
                                int output_height) {
    PASS_REQUIRE(cc_indexed_reserve(renderer, 12, NULL, 0));
    PASS_REQUIRE(cc_indexed_reserve_passes(renderer, 12, NULL, 0));
    CcIndexedProgram *program = pass_program(renderer);
    CcIndexedMesh *mesh = pass_mesh(renderer);
    CcIndexedTexture *white = pass_white(renderer);
    CcIndexedTarget *stripes =
        pass_target_size(renderer, CC_INDEXED_RGBA8, false, 32, 32);
    CcIndexedTarget *scratch =
        pass_target_size(renderer, CC_INDEXED_RGBA8, false, (unsigned)output_width * 2,
                         (unsigned)output_height + 16);
    CcIndexedTarget *composite =
        pass_target_size(renderer, CC_INDEXED_RGBA8, false, (unsigned)output_width,
                         (unsigned)output_height);
    CcIndexedTarget *cleared = pass_target(renderer, CC_INDEXED_RGBA8, false);
    float tint[1][4] = {{1, 1, 1, 1}};
    CcIndexedDraw draw = {.program = program,
                          .mesh = mesh,
                          .fragment_uniforms = (const float(*)[4])tint,
                          .fragment_uniform_count = 1,
                          .textures = {white},
                          .texture_count = 1,
                          .index_count = 6};
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    pass_stripes(renderer, stripes, &draw, tint);
    draw.textures[0] = cc_indexed_target_texture(renderer, stripes);
    for (size_t lane = 0; lane < 4; ++lane)
        tint[0][lane] = 1.0f;
    for (int index = 0; index < 2; ++index) {
        CcIndexedPass pass = pass_rectangle(
            scratch, index ? CC_INDEXED_LOAD : CC_INDEXED_CLEAR,
            (CcViewport){0, -index * output_height, output_width * 2,
                         output_height * 2},
            (CcViewport){0, 0, output_width * 2, output_height + (index ? 0 : 16)});
        CcIndexedPass invalid = pass;
        invalid.scissor.y = -1;
        PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &invalid, NULL, 0));
        invalid.viewport.y = INT_MIN;
        PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &invalid, NULL, 0));
        PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
        PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    }
    draw.textures[0] = cc_indexed_target_texture(renderer, scratch);
    int half = (output_height + 16) / 2;
    for (int index = 0; index < 2; ++index) {
        CcIndexedPass pass =
            pass_rectangle(composite, index ? CC_INDEXED_LOAD : CC_INDEXED_CLEAR,
                           (CcViewport){0, 0, output_width, output_height},
                           (CcViewport){0, index ? half - 4 : 0, output_width,
                                        half + (index ? 4 : 0)});
        PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
        tint[0][0] = index ? 0.0f : 1.0f;
        tint[0][1] = index ? 1.0f : 0.0f;
        PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    }
    /* An empty attachment intersection preserves LOAD contents, consumes no
     * fragments and still participates in the ordered pass/store sequence. */
    CcIndexedPass empty = pass_rectangle(
        composite, CC_INDEXED_LOAD, (CcViewport){0, 0, output_width, output_height},
        (CcViewport){output_width + 7, 0, 23, output_height});
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &empty, NULL, 0));
    draw.textures[0] = white;
    tint[0][0] = tint[0][1] = tint[0][2] = tint[0][3] = 1.0f;
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    empty.target = cleared;
    empty.color_load = CC_INDEXED_CLEAR;
    empty.clear_color = (CcColor){0.2f, 0.4f, 0.6f, 1.0f};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &empty, NULL, 0));
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    CcIndexedPass drawable = pass_rectangle(
        NULL, CC_INDEXED_CLEAR, (CcViewport){0, 0, drawable_width / 2, drawable_height},
        (CcViewport){0, 0, drawable_width / 2, drawable_height});
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &drawable, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, composite);
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    drawable.color_load = CC_INDEXED_LOAD;
    drawable.viewport.x = drawable_width / 2;
    drawable.scissor.x = drawable_width / 2;
    drawable.viewport.width = drawable_width - drawable_width / 2;
    drawable.scissor.width = drawable.viewport.width;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &drawable, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, cleared);
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_program_release(renderer, &program, NULL, 0));
    PASS_REQUIRE(cc_indexed_mesh_release(renderer, &mesh, NULL, 0));
    PASS_REQUIRE(cc_indexed_texture_release(renderer, &white, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &stripes, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &scratch, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &composite, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &cleared, NULL, 0));
}

static void pass_extended_pixels(const CcFramebuffer *frame, int output_height) {
    static const uint8_t blue[] = {0, 0, 255, 255};
    static const uint8_t red[] = {255, 0, 0, 255};
    static const uint8_t green[] = {0, 255, 0, 255};
    static const uint8_t clear[] = {51, 102, 153, 255};
    int second_scissor = (output_height + 16) / 2 - 4;
    for (int y = 1; y < frame->height; ++y) {
        int composite_y =
            (int)(((int64_t)y * 2 + 1) * output_height / ((int64_t)frame->height * 2));
        int scratch_y = (int)(((int64_t)composite_y * 2 + 1) * (output_height + 16) /
                              ((int64_t)output_height * 2));
        const uint8_t *expected =
            scratch_y < output_height / 2 || scratch_y >= output_height
                ? blue
                : (composite_y < second_scissor ? red : green);
        pass_pixel(frame, frame->width / 8, y, expected);
        pass_pixel(frame, frame->width * 3 / 8, y, expected);
        pass_pixel(frame, frame->width * 3 / 4, y, clear);
    }
}

#endif
