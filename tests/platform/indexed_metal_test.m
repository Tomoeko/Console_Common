#include "console_common/platform/indexed.h"

#include <stdio.h>
#include <stdlib.h>

static void require(bool condition, const char *expression, int line) {
    if (!condition) {
        fprintf(stderr, "indexed_metal_test.m:%d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define REQUIRE(condition) require((condition), #condition, __LINE__)

static const char vertex_source[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[attribute(0)]];\n"
    "    float4 color [[attribute(1)]];\n"
    "};\n"
    "struct Output {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "vertex Output vertex_main(Input input [[stage_in]],\n"
    "    constant float4& offset [[buffer(3)]]) {\n"
    "    Output output;\n"
    "    output.position = input.position + offset;\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n";

static const char fragment_source[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "fragment float4 fragment_main(Input input [[stage_in]],\n"
    "    constant float4& tint [[buffer(2)]],\n"
    "    texture2d<float> image [[texture(0)]],\n"
    "    sampler image_sampler [[sampler(0)]]) {\n"
    "    float4 color = input.color * tint;\n"
    "    return color * image.sample(image_sampler, float2(0.5f));\n"
    "}\n";

static CcIndexedProgram *create_program(CcIndexedRenderer *renderer, bool constant) {
    CcIndexedProgramDescription description = {0};
    description.metal_vertex_source = vertex_source;
    description.metal_fragment_source = fragment_source;
    description.metal_vertex_entry = "vertex_main";
    description.metal_fragment_entry = "fragment_main";
    description.attribute_count = 2;
    description.attributes[0] = (CcIndexedAttribute){"position", 0};
    description.attributes[1] = (CcIndexedAttribute){"color", 16};
    description.vertex_stride = 32;
    description.vertex_uniform_count = 1;
    description.fragment_uniform_count = 1;
    description.vertex_uniforms[0] = "offset";
    description.fragment_uniforms[0] = "tint";
    description.metal_vertex_uniform_buffer = 3;
    description.metal_fragment_uniform_buffer = 2;
    description.texture_uniforms[0] = "image";
    description.texture_count = 1;
    description.state.depth_test = true;
    description.state.depth_write = true;
    description.state.depth_compare = CC_INDEXED_LESS_EQUAL;
    for (size_t index = 0; index < 4; ++index)
        description.state.color_write[index] = true;
    if (constant) {
        description.state.blend = true;
        description.state.source_rgb = CC_INDEXED_CONSTANT_COLOR;
        description.state.destination_rgb = CC_INDEXED_SOURCE_COLOR;
        description.state.source_alpha = CC_INDEXED_ONE;
        description.state.destination_alpha = CC_INDEXED_ZERO;
        for (size_t index = 0; index < 4; ++index)
            description.state.blend_color[index] = (float)(index + 1) * 0.25f;
    }
    char error[256];
    CcIndexedProgram *program =
        cc_indexed_program_create(renderer, &description, error, sizeof(error));
    if (!program)
        fprintf(stderr, "%s\n", error);
    REQUIRE(program);
    return program;
}

static CcIndexedMesh *create_mesh(CcIndexedRenderer *renderer, bool dynamic) {
    const float vertices[3][8] = {{-0.75f, -0.75f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
                                  {0.75f, -0.75f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                                  {0.0f, 0.75f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f}};
    const uint16_t indices[] = {0, 1, 2};
    CcIndexedMeshDescription description = {vertices, 3, sizeof(vertices[0]), indices,
                                            3};
    CcIndexedMesh *mesh =
        dynamic ? cc_indexed_mesh_create_dynamic(renderer, &description, NULL, 0)
                : cc_indexed_mesh_create(renderer, &description, NULL, 0);
    REQUIRE(mesh);
    return mesh;
}

static CcIndexedTexture *create_texture(CcIndexedRenderer *renderer) {
    const uint8_t rgba[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    CcIndexedTextureDescription description = {0};
    description.levels[0] = (CcIndexedMip){rgba, sizeof(rgba), 3, 2};
    description.levels[1] = (CcIndexedMip){rgba, 4, 1, 1};
    description.level_count = 2;
    description.min_filter = CC_INDEXED_LINEAR;
    description.mag_filter = CC_INDEXED_LINEAR;
    description.mip_filter = CC_INDEXED_MIP_NEAREST;
    description.wrap_s = CC_INDEXED_REPEAT;
    description.wrap_t = CC_INDEXED_REPEAT;
    description.max_lod = 13.0f;
    description.max_anisotropy = 1;
    CcIndexedTexture *texture =
        cc_indexed_texture_create(renderer, &description, NULL, 0);
    REQUIRE(texture);
    return texture;
}

static void release_order_test(CcIndexedRenderer *renderer) {
    CcIndexedProgram *programs[3];
    CcIndexedMesh *meshes[3];
    CcIndexedTexture *textures[3];
    for (size_t index = 0; index < 3; ++index) {
        programs[index] = create_program(renderer, false);
        meshes[index] = create_mesh(renderer, index != 0);
        textures[index] = create_texture(renderer);
    }
    const size_t order[] = {1, 2, 0};
    for (size_t index = 0; index < 3; ++index) {
        size_t removed = order[index];
        REQUIRE(cc_indexed_program_release(renderer, &programs[removed], NULL, 0));
        REQUIRE(cc_indexed_mesh_release(renderer, &meshes[removed], NULL, 0));
        REQUIRE(cc_indexed_texture_release(renderer, &textures[removed], NULL, 0));
        REQUIRE(!programs[removed] && !meshes[removed] && !textures[removed]);
    }
    REQUIRE(!cc_indexed_program_release(renderer, NULL, NULL, 0));
    REQUIRE(!cc_indexed_mesh_release(renderer, NULL, NULL, 0));
    REQUIRE(!cc_indexed_texture_release(renderer, NULL, NULL, 0));
}

static void capture_pixel_test(const CcFramebuffer *frame, int x, int y) {
    const uint8_t expected[] = {51, 102, 153, 255};
    const uint8_t *pixel = frame->rgba + (size_t)y * frame->stride + (size_t)x * 4;
    for (size_t lane = 0; lane < 4; ++lane) {
        int difference = (int)pixel[lane] - expected[lane];
        REQUIRE(difference >= -1 && difference <= 1);
    }
}

static void capture_test(CcPlatform *platform, CcIndexedRenderer *renderer) {
    CcFramebuffer frame;
    REQUIRE(!cc_platform_capture_frame(platform, &frame));
    REQUIRE(cc_platform_capture_begin(platform, &frame));
    REQUIRE(!frame.rgba && frame.width > 0 && frame.height > 0);
    REQUIRE(!cc_platform_capture_frame(platform, &frame));
    CcIndexedFrame indexed = {.clear_color = {0.2f, 0.4f, 0.6f, 1.0f},
                              .clear_color_enabled = true};
    REQUIRE(cc_indexed_begin(renderer, &indexed, NULL, 0));
    REQUIRE(cc_indexed_end(renderer, NULL, 0));
    REQUIRE(cc_platform_capture_frame(platform, &frame));
    REQUIRE(frame.rgba && frame.stride >= (size_t)frame.width * 4);
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x)
            capture_pixel_test(&frame, x, y);
    cc_platform_capture_end(platform);
    REQUIRE(!cc_platform_capture_frame(platform, &frame));

    REQUIRE(cc_indexed_begin(renderer, &indexed, NULL, 0));
    REQUIRE(cc_platform_capture_begin(platform, &frame));
    REQUIRE(cc_indexed_end(renderer, NULL, 0));
    REQUIRE(!cc_platform_capture_frame(platform, &frame));
    REQUIRE(cc_indexed_begin(renderer, &indexed, NULL, 0));
    REQUIRE(cc_indexed_end(renderer, NULL, 0));
    REQUIRE(cc_platform_capture_frame(platform, &frame));
    capture_pixel_test(&frame, frame.width / 2, frame.height / 2);
    cc_platform_capture_end(platform);

    /* The original quad route still captures its target, including its fader.
     * Inspect the fitted picture center so non-16:9 consumer bars stay valid. */
    REQUIRE(cc_platform_capture_begin(platform, &frame));
    cc_platform_set_fade_alpha(platform, 0.0f);
    cc_platform_begin(platform, indexed.clear_color);
    cc_platform_end(platform);
    REQUIRE(cc_platform_capture_frame(platform, &frame));
    capture_pixel_test(&frame, frame.width / 2, frame.height / 2);
    cc_platform_capture_end(platform);
    REQUIRE(!cc_platform_capture_frame(platform, &frame));
}

int main(void) {
    CcPlatform *platform = cc_platform_create("Indexed Metal validation", 160, 90);
    if (!platform)
        return 77;
    char error[256];
    CcIndexedRenderer *renderer = cc_indexed_create(platform, error, sizeof(error));
    REQUIRE(renderer);
    REQUIRE(cc_indexed_reserve(renderer, 2, error, sizeof(error)));
    release_order_test(renderer);
    CcIndexedProgram *program = create_program(renderer, false);
    CcIndexedProgram *constant_program = create_program(renderer, true);
    CcIndexedMesh *mesh = create_mesh(renderer, true);
    CcIndexedMesh *immutable = create_mesh(renderer, false);
    CcIndexedTexture *texture = create_texture(renderer);
    float replacement[3][8] = {{-0.5f, -0.5f, 0.5f, 1, 1, 0, 0, 1},
                               {0.5f, -0.5f, 0.5f, 1, 0, 1, 0, 1},
                               {0, 0.5f, 0.5f, 1, 0, 0, 1, 1}};
    REQUIRE(!cc_indexed_mesh_update(renderer, immutable, replacement,
                                    sizeof(replacement), NULL, 0));
    REQUIRE(!cc_indexed_mesh_update(renderer, mesh, replacement,
                                    sizeof(replacement) - 1, NULL, 0));
    REQUIRE(
        !cc_indexed_mesh_update(renderer, mesh, NULL, sizeof(replacement), NULL, 0));
    CcIndexedRenderer *other = cc_indexed_create(platform, error, sizeof(error));
    REQUIRE(other);
    REQUIRE(!cc_indexed_program_release(other, &program, NULL, 0));
    REQUIRE(!cc_indexed_mesh_release(other, &mesh, NULL, 0));
    REQUIRE(!cc_indexed_texture_release(other, &texture, NULL, 0));
    REQUIRE(!cc_indexed_mesh_update(other, mesh, replacement, sizeof(replacement), NULL,
                                    0));
    cc_indexed_destroy(other);
    const float offset[1][4] = {{0, 0, 0, 0}};
    const float tint[1][4] = {{1, 1, 1, 1}};
    CcIndexedDraw draw = {.program = program,
                          .mesh = mesh,
                          .vertex_uniforms = offset,
                          .vertex_uniform_count = 1,
                          .fragment_uniforms = tint,
                          .fragment_uniform_count = 1,
                          .textures = {texture},
                          .texture_count = 1,
                          .index_count = 3};
    REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
    REQUIRE(!cc_indexed_end(renderer, NULL, 0));
    for (size_t index = 0; index < 8; ++index) {
        replacement[0][4] = (float)index / 8.0f;
        REQUIRE(cc_indexed_mesh_update(renderer, mesh, replacement, sizeof(replacement),
                                       error, sizeof(error)));
        CcIndexedFrame frame = {.clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
                                .clear_depth = 1.0f,
                                .clear_color_enabled = true,
                                .clear_depth_enabled = index & 1,
                                .depth_attachment = index & 1};
        REQUIRE(cc_indexed_begin(renderer, &frame, error, sizeof(error)));
        REQUIRE(!cc_indexed_reserve(renderer, 4, NULL, 0));
        REQUIRE(!cc_indexed_program_release(renderer, &program, NULL, 0));
        REQUIRE(!cc_indexed_mesh_release(renderer, &mesh, NULL, 0));
        REQUIRE(!cc_indexed_texture_release(renderer, &texture, NULL, 0));
        REQUIRE(!cc_indexed_mesh_update(renderer, mesh, replacement,
                                        sizeof(replacement), NULL, 0));
        REQUIRE(cc_indexed_draw(renderer, &draw, error, sizeof(error)));
        draw.first_index = 1;
        REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
        draw.first_index = 0;
        draw.program = constant_program;
        REQUIRE(cc_indexed_draw(renderer, &draw, error, sizeof(error)));
        REQUIRE(!cc_indexed_draw(renderer, &draw, NULL, 0));
        REQUIRE(cc_indexed_end(renderer, error, sizeof(error)));
        draw.program = program;
    }
    /* Last native commands remain in flight. Releasing the C handles must
     * retain their encoded native objects until those commands complete. */
    REQUIRE(cc_indexed_program_release(renderer, &program, error, sizeof(error)));
    REQUIRE(
        cc_indexed_program_release(renderer, &constant_program, error, sizeof(error)));
    REQUIRE(cc_indexed_mesh_release(renderer, &mesh, error, sizeof(error)));
    REQUIRE(cc_indexed_mesh_release(renderer, &immutable, error, sizeof(error)));
    REQUIRE(cc_indexed_texture_release(renderer, &texture, error, sizeof(error)));
    REQUIRE(!program && !mesh && !immutable && !texture);
    REQUIRE(cc_indexed_program_release(renderer, &program, NULL, 0));
    REQUIRE(cc_indexed_mesh_release(renderer, &mesh, NULL, 0));
    REQUIRE(cc_indexed_texture_release(renderer, &texture, NULL, 0));
    bool completed = cc_indexed_wait(renderer, error, sizeof(error));
    if (!completed)
        fprintf(stderr, "%s\n", error);
    REQUIRE(completed);
    capture_test(platform, renderer);
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Indexed Metal GPU submission tests passed.");
    return EXIT_SUCCESS;
}
