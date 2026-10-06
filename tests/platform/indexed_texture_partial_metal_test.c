#include "../support/indexed_partial_texture_fixture.h"
#include "../support/indexed_texture_fixture.h"

/* Explicit LOD exercises every physical retained Metal mip and final-level
 * clamping. This diagnostic program does not represent a native PS3 shader. */
static const char partial_fragment[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[position]];\n"
    "    float2 uv;\n"
    "};\n"
    "fragment float4 fragment_main(Input input [[stage_in]],\n"
    "    constant float4 &sample_level [[buffer(2)]],\n"
    "    texture2d<float> image [[texture(0)]],\n"
    "    sampler image_sampler [[sampler(0)]]) {\n"
    "    return image.sample(image_sampler, input.uv, level(sample_level.x));\n"
    "}\n";

static void sample_level(CcPlatform *platform, CcIndexedRenderer *renderer,
                         const CcIndexedDraw *draw, const uint8_t expected[4]) {
    CcFramebuffer capture;
    CHECK(cc_platform_capture_begin(platform, &capture));
    const CcIndexedFrame frame = {
        .clear_color = {0, 0, 0, 1},
        .clear_color_enabled = true,
    };
    CHECK(cc_indexed_begin(renderer, &frame, NULL, 0));
    CHECK(cc_indexed_draw(renderer, draw, NULL, 0));
    CHECK(cc_indexed_end(renderer, NULL, 0));
    CHECK(cc_platform_capture_frame(platform, &capture));
    texture_pixels(&capture, expected);
    cc_platform_capture_end(platform);
}

int main(void) {
    CcPlatform *platform = cc_platform_create("Partial mip validation", 160, 90);
    CHECK(platform);
    CcIndexedRenderer *renderer = cc_indexed_create(platform, NULL, 0);
    CHECK(renderer);
    TextureFixture fixture;
    texture_fixture_open(&fixture, renderer);
    texture_frame(&fixture, false, 0, mip_colors[0]);
    CcIndexedProgramDescription description = {
        .gles_vertex_source = vertex_glsl,
        .gles_fragment_source = fragment_glsl,
        .metal_vertex_source = vertex_metal,
        .metal_fragment_source = partial_fragment,
        .metal_vertex_entry = "vertex_main",
        .metal_fragment_entry = "fragment_main",
        .attributes = {{"position", 0}, {"coordinates", 16}},
        .attribute_count = 2,
        .vertex_stride = 32,
        .vertex_uniforms = {"uv_scale"},
        .vertex_uniform_count = 1,
        .fragment_uniforms = {"sample_level"},
        .fragment_uniform_count = 1,
        .texture_uniforms = {"image"},
        .texture_count = 1,
        .metal_vertex_uniform_buffer = 1,
        .metal_fragment_uniform_buffer = 2,
        .state = {.color_write = {true, true, true, true}},
    };
    CcIndexedProgram *program =
        cc_indexed_program_create(renderer, &description, NULL, 0);
    CHECK(program);
    uint8_t pixels[PARTIAL_MIPS_BYTES];
    CcIndexedTextureDescription texture_description =
        partial_texture_description(pixels);
    CcIndexedTexture *textures[2] = {
        cc_indexed_texture_create(renderer, &texture_description, NULL, 0),
        cc_indexed_texture_create_dynamic(renderer, &texture_description, NULL, 0),
    };
    CHECK(textures[0] && textures[1]);
    memset(pixels, 0xa7, sizeof(pixels));
    float lod[1][4] = {{0}};
    CcIndexedDraw draw = fixture.draw;
    draw.program = program;
    draw.fragment_uniforms = (const float(*)[4])lod;
    draw.fragment_uniform_count = 1;
    for (unsigned owner = 0; owner < 2; ++owner) {
        draw.textures[0] = textures[owner];
        for (unsigned level = 0; level < PARTIAL_MIPS_LEVELS; ++level) {
            lod[0][0] = (float)level;
            sample_level(platform, renderer, &draw, mip_colors[level]);
        }
        lod[0][0] = 12;
        sample_level(platform, renderer, &draw, mip_colors[4]);
    }
    const uint8_t changed[4] = {211, 73, 47, 255};
    for (size_t pixel = 0; pixel < 32; pixel += 4)
        memcpy(pixels + pixel, changed, 4);
    CHECK(cc_indexed_texture_update(renderer, textures[1], 4, pixels, 32, NULL, 0));
    draw.textures[0] = textures[1];
    lod[0][0] = 4;
    sample_level(platform, renderer, &draw, changed);
    CHECK(cc_indexed_texture_release(renderer, &textures[0], NULL, 0));
    CHECK(cc_indexed_texture_release(renderer, &textures[1], NULL, 0));
    CHECK(cc_indexed_program_release(renderer, &program, NULL, 0));
    texture_fixture_close(&fixture);
    cc_indexed_destroy(renderer);
    cc_platform_destroy(platform);
    puts("Metal five-level partial chain: 13 captures, all supplied levels, "
         "last-level clamping and dynamic update passed.");
    return 0;
}
