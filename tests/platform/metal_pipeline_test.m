#include "platform_metal_internal.h"
#include "console_common/render/viewport.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned pipeline_count(CcMetalState *state) {
    unsigned count = 0;
    for (unsigned kind = 0; kind < 2; ++kind) {
        for (unsigned blend = 0; blend < CC_MATERIAL_PIPELINE_VARIANTS; ++blend) {
            count += state->material_pipelines[kind][blend] != nil;
        }
    }
    return count;
}

static void test_material_preparation(CcPlatform *platform, CcMetalState *state) {
    assert(pipeline_count(state) == 4);
    for (unsigned depth = 0; depth < 17; ++depth) {
        assert(state->depth_states[depth]);
    }

    CcMaterialQuad quad = {0};
    quad.has_blend_mode = true;
    quad.blend_mode[0] = 1;
    quad.blend_mode[1] = 1;
    quad.blend_mode[2] = 1;
    cc_platform_prepare_material(platform, &quad);
    assert(pipeline_count(state) == 5);
    id<MTLRenderPipelineState> simple = state->material_pipelines[0][9];
    assert(simple);

    quad.tev_stage_count = 1;
    cc_platform_prepare_material(platform, &quad);
    assert(pipeline_count(state) == 6);
    id<MTLRenderPipelineState> tev = state->material_pipelines[1][9];
    assert(tev);
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        cc_platform_prepare_material(platform, &quad);
        cc_platform_draw_material_quad(platform, &quad);
    }
    assert(state->material_pipelines[0][9] == simple);
    assert(state->material_pipelines[1][9] == tev);
    assert(pipeline_count(state) == 6);
    assert(state->batch_count == 1);
    assert(state->batches[0].state.kind == CC_BATCH_TEV);
    assert(state->batches[0].state.blend_key == 9);

    quad.blend_mode[1] = 8;
    cc_platform_prepare_material(platform, &quad);
    cc_platform_draw_material_quad(platform, &quad);
    assert(pipeline_count(state) == 6);
    assert(!cc_metal_material_pipeline(state, CC_BATCH_BASIC, 9));
    assert(!cc_metal_material_pipeline(state, CC_BATCH_TEV, 65));
}

static void test_signed_quad(CcPlatform *platform, CcMetalState *state) {
    cc_platform_begin(platform, (CcColor){0, 0, 0, 1});
    size_t before = state->vertex_count;
    CcQuad quad = {.x = 20,
                   .y = 15,
                   .width = -8,
                   .height = -5,
                   .u0 = 1,
                   .v0 = 1,
                   .u1 = 0,
                   .v1 = 0,
                   .color = {1, 1, 1, 1}};
    cc_platform_draw_quad(platform, &quad);
    assert(state->vertex_count == before + 6);
    assert(state->vertices[before].x == 20);
    assert(state->vertices[before + 1].x == 12);
    assert(state->vertices[before + 2].y == 10);
    quad.width = 0;
    cc_platform_draw_quad(platform, &quad);
    assert(state->vertex_count == before + 6);
    quad.width = -8;
    quad.height = 0;
    cc_platform_draw_quad(platform, &quad);
    assert(state->vertex_count == before + 6);
}

static void read_target_pixels(CcMetalState *state, uint32_t target,
                               id<MTLBuffer> readback) {
    id<MTLCommandBuffer> command = [state->command_queue commandBuffer];
    assert(command);
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    assert(blit);
    [blit copyFromTexture:state->textures[target]
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(4, 4, 1)
                        toBuffer:readback
               destinationOffset:0
          destinationBytesPerRow:256
        destinationBytesPerImage:1024];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    assert(command.status == MTLCommandBufferStatusCompleted);
}

static void assert_target_clip(CcPlatform *platform, CcMetalState *state,
                               uint32_t target, id<MTLBuffer> readback,
                               const CcClipRect *clip, unsigned first, unsigned last) {
    assert(cc_platform_begin_target(platform, target, (CcColor){0, 0, 0, 1}));
    cc_platform_set_clip(platform, clip);
    CcQuad quad = {.width = 4, .height = 4, .color = {1, 0, 0, 1}};
    cc_platform_draw_quad(platform, &quad);
    cc_platform_end(platform);
    cc_wait_for_metal(state);
    read_target_pixels(state, target, readback);
    const uint8_t *pixels = readback.contents;
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const uint8_t *bgra = pixels + y * 256 + x * 4;
            bool inside = x >= first && x < last && y >= first && y < last;
            assert(bgra[0] == 0 && bgra[1] == 0 && bgra[3] == 255);
            assert(bgra[2] == (inside ? 255 : 0));
        }
    }
}

static void test_clip_rendering(CcPlatform *platform, CcMetalState *state) {
    uint32_t target = cc_platform_create_render_texture(platform);
    assert(target);
    /* Render targets use private GPU storage; read through an aligned blit. */
    id<MTLBuffer> readback =
        [state->device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
    assert(readback);
    assert_target_clip(platform, state, target, readback, NULL, 0, 4);
    CcClipRect clip = {1.25f, 1.25f, 1.5f, 1.5f};
    assert_target_clip(platform, state, target, readback, &clip, 1, 3);
    const CcClipRect invalid[] = {{NAN, 0, 4, 4},
                                  {0, NAN, 4, 4},
                                  {0, 0, INFINITY, 4},
                                  {0, 0, 4, -INFINITY},
                                  {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX},
                                  {0, 0, -4, 4}};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
        assert_target_clip(platform, state, target, readback, &invalid[index], 0, 0);
    cc_platform_destroy_texture(platform, target);
}

static CcMaterialQuad sampling_quad(uint32_t texture, unsigned stage_count) {
    CcMaterialQuad quad = {0};
    quad.texture_count = 1;
    quad.textures[0] = texture;
    quad.has_blend_mode = true;
    quad.tev_stage_count = (uint8_t)stage_count;
    memset(quad.tev_swap_table, 0xe4, sizeof(quad.tev_swap_table));
    quad.tev_stages[0][4] = 0x8f;
    quad.tev_stages[0][5] = 0xfa;
    quad.tev_stages[0][7] = 1;
    quad.tev_stages[0][8] = 0x47;
    quad.tev_stages[0][9] = 0x75;
    quad.tev_stages[0][11] = 1;
    for (unsigned component = 0; component < 4; ++component)
        quad.registers[1][component] = 1;
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
        quad.vertices[vertex].x = vertex & 1 ? 1 : 0;
        quad.vertices[vertex].y = vertex & 2 ? 4 : 0;
        quad.vertices[vertex].color = (CcColor){1, 1, 1, 1};
        for (unsigned slot = 0; slot < 2; ++slot) {
            quad.vertices[vertex].uv[slot][0] = 0.4f;
            quad.vertices[vertex].uv[slot][1] = 0.5f;
        }
    }
    return quad;
}

static void test_material_sampling(CcPlatform *platform, CcMetalState *state) {
    const uint8_t gradient[8] = {0, 0, 0, 255, 255, 255, 255, 255};
    uint32_t texture = cc_platform_create_texture(platform, 2, 1, gradient);
    uint32_t target = cc_platform_create_render_texture(platform);
    assert(texture && target);
    id<MTLBuffer> readback =
        [state->device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
    assert(readback);
    for (unsigned stage_count = 0; stage_count <= 1; ++stage_count) {
        assert(cc_platform_begin_target(platform, target, (CcColor){0, 0, 0, 1}));
        for (unsigned column = 0; column < 4; ++column) {
            CcMaterialQuad quad = sampling_quad(texture, stage_count);
            quad.nearest[0] = !(column & 1);
            for (unsigned vertex = 0; vertex < 4; ++vertex)
                quad.vertices[vertex].x += (float)column;
            cc_platform_draw_material_quad(platform, &quad);
        }
        assert(state->batch_count == 4);
        cc_platform_end(platform);
        cc_wait_for_metal(state);
        read_target_pixels(state, target, readback);
        const uint8_t *pixels = readback.contents;
        for (unsigned y = 0; y < 4; ++y) {
            for (unsigned x = 0; x < 4; ++x) {
                const uint8_t *bgra = pixels + y * 256 + x * 4;
                unsigned expected = x & 1 ? 77 : 0;
                for (unsigned component = 0; component < 3; ++component)
                    assert(bgra[component] >= (expected ? expected - 1 : 0) &&
                           bgra[component] <= expected + (expected ? 1 : 0));
            }
        }
    }
    CcMaterialQuad mixed = sampling_quad(texture, 0);
    mixed.texture_count = 2;
    mixed.textures[1] = texture;
    mixed.nearest[0] = true;
    mixed.konst_colors[3][3] = 0.5f;
    assert(cc_platform_begin_target(platform, target, (CcColor){0, 0, 0, 1}));
    cc_platform_draw_material_quad(platform, &mixed);
    CcQuad basic = {.x = 1,
                    .width = 1,
                    .height = 4,
                    .u0 = 0.4f,
                    .u1 = 0.4f,
                    .v0 = 0.5f,
                    .v1 = 0.5f,
                    .color = {1, 1, 1, 1},
                    .texture = texture};
    cc_platform_draw_quad(platform, &basic);
    cc_platform_end(platform);
    cc_wait_for_metal(state);
    read_target_pixels(state, target, readback);
    const uint8_t *pixels = readback.contents;
    for (unsigned component = 0; component < 3; ++component) {
        assert(pixels[component] >= 37 && pixels[component] <= 39);
        assert(pixels[4 + component] >= 76 && pixels[4 + component] <= 78);
    }
    cc_platform_destroy_texture(platform, target);
    cc_platform_destroy_texture(platform, texture);
}

static void assert_capture_pixel(const CcFramebuffer *frame, int x, int y, uint8_t r,
                                 uint8_t g, uint8_t b) {
    const uint8_t *rgba = frame->rgba + (size_t)y * frame->stride + (size_t)x * 4;
    assert(rgba[0] == r && rgba[1] == g && rgba[2] == b && rgba[3] == 255);
}

static void test_framebuffer_capture(CcPlatform *platform, CcMetalState *state) {
    CcFramebuffer frame = {0};
    assert(!cc_platform_capture_begin(NULL, &frame));
    assert(!cc_platform_capture_begin(platform, NULL));
    assert(!cc_platform_capture_frame(platform, &frame));
    state->view = [[CcMetalView alloc] initWithFrame:NSMakeRect(0, 0, 37, 31)];
    NSSize backing = [state->view convertSizeToBacking:state->view.bounds.size];
    int width = (int)llround(backing.width);
    int height = (int)llround(backing.height);
    assert(cc_platform_capture_begin(platform, &frame));
    assert(frame.width == width && frame.height == height &&
           frame.stride == (size_t)width * 4);
    assert(!frame.rgba);
    assert(!cc_platform_capture_begin(platform, &frame));
    assert(!cc_platform_capture_frame(platform, &frame));
    cc_platform_begin(platform, (CcColor){0, 0, 1, 1});
    CcQuad red = {.width = CC_FRAME_WIDTH / 2,
                  .height = CC_FRAME_HEIGHT / 2,
                  .color = {1, 0, 0, 1}};
    CcQuad green = {.x = CC_FRAME_WIDTH / 2,
                    .y = CC_FRAME_HEIGHT / 2,
                    .width = CC_FRAME_WIDTH / 2,
                    .height = CC_FRAME_HEIGHT / 2,
                    .color = {0, 1, 0, 1}};
    cc_platform_draw_quad(platform, &red);
    cc_platform_draw_quad(platform, &green);
    cc_platform_end(platform);
    assert(cc_platform_capture_frame(platform, &frame));
    CcViewport content = cc_viewport_fit(frame.width, frame.height);
    int left = content.x + content.width / 4;
    int right = content.x + content.width * 3 / 4;
    int top = content.y + content.height / 4;
    int bottom = content.y + content.height * 3 / 4;
    assert_capture_pixel(&frame, left, top, 255, 0, 0);
    assert_capture_pixel(&frame, right, top, 0, 0, 255);
    assert_capture_pixel(&frame, left, bottom, 0, 0, 255);
    assert_capture_pixel(&frame, right, bottom, 0, 255, 0);
    assert_capture_pixel(&frame, 0, 0, 0, 0, 0);
    const uint8_t *borrowed = frame.rgba;
    uint32_t preview = cc_platform_create_render_texture(platform);
    assert(preview &&
           cc_platform_begin_target(platform, preview, (CcColor){1, 1, 1, 1}));
    cc_platform_end(platform);
    assert(cc_platform_capture_frame(platform, &frame) && frame.rgba == borrowed);
    assert_capture_pixel(&frame, left, top, 255, 0, 0);
    cc_platform_destroy_texture(platform, preview);
    [state->view setFrameSize:NSMakeSize(73, 59)];
    cc_platform_set_fade_alpha(platform, 1);
    cc_platform_begin(platform, (CcColor){1, 1, 1, 1});
    cc_platform_end(platform);
    assert(cc_platform_capture_frame(platform, &frame));
    assert(frame.width == width && frame.height == height && frame.rgba == borrowed);
    assert_capture_pixel(&frame, left, top, 0, 0, 0);
    assert_capture_pixel(&frame, right, bottom, 0, 0, 0);
    cc_platform_capture_end(platform);
    cc_platform_capture_end(platform);
    assert(!cc_platform_capture_frame(platform, &frame));
    backing = [state->view convertSizeToBacking:state->view.bounds.size];
    assert(cc_platform_capture_begin(platform, &frame));
    assert(frame.width == (int)llround(backing.width) &&
           frame.height == (int)llround(backing.height) && !frame.rgba);
    cc_platform_capture_end(platform);
    cc_platform_set_fade_alpha(platform, 0);
}

static unsigned count_partial_pixels(const CcFramebuffer *frame) {
    unsigned count = 0;
    for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            const uint8_t *pixel =
                frame->rgba + (size_t)y * frame->stride + (size_t)x * 4;
            assert(pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 255);
            count += pixel[0] > 0 && pixel[0] < 255;
        }
    }
    return count;
}

static void draw_diagonal(CcPlatform *platform) {
    const CcDrawVertex vertices[4] = {
        {.x = 103, .y = 61, .color = {1, 1, 1, 1}},
        {.x = 457, .y = 127, .color = {1, 1, 1, 1}},
        {.x = 151, .y = 359, .color = {1, 1, 1, 1}},
        {.x = 521, .y = 411, .color = {1, 1, 1, 1}},
    };
    cc_platform_begin(platform, (CcColor){0, 0, 0, 1});
    cc_platform_draw_vertices(platform, vertices, 0);
    cc_platform_end(platform);
}

static void draw_rotated_texture(CcPlatform *platform, uint32_t texture,
                                 unsigned stage_count) {
    CcMaterialQuad quad = sampling_quad(texture, stage_count);
    quad.nearest[0] = true;
    quad.wrap_s[0] = 1;
    quad.wrap_t[0] = 1;
    const float uv[4][2] = {
        {0.17f, 0.29f},
        {6.17f, 3.29f},
        {-2.83f, 6.29f},
        {3.17f, 9.29f},
    };
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
        quad.vertices[vertex].x = vertex & 1 ? CC_FRAME_WIDTH : 0;
        quad.vertices[vertex].y = vertex & 2 ? CC_FRAME_HEIGHT : 0;
        memcpy(quad.vertices[vertex].uv[0], uv[vertex], sizeof(uv[vertex]));
    }
    cc_platform_prepare_material(platform, &quad);
    cc_platform_begin(platform, (CcColor){0, 0, 0, 1});
    cc_platform_draw_material_quad(platform, &quad);
    cc_platform_end(platform);
}

static void test_texture_sample_coverage(CcPlatform *platform, CcMetalState *state) {
    const uint8_t checker[] = {
        0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255,
    };
    uint32_t texture = cc_platform_create_texture(platform, 2, 2, checker);
    assert(texture);
    [state->view setFrameSize:NSMakeSize(80, 60)];
    CcFramebuffer frame = {0};
    assert(cc_platform_capture_begin(platform, &frame));
    /* The full-screen primitive has no edges inside the content. Gray coverage
     * therefore comes from texture samples, not ordinary polygon-edge MSAA. */
    for (unsigned stages = 0; stages <= 1; ++stages) {
        assert(cc_platform_set_antialiasing(platform, false));
        draw_rotated_texture(platform, texture, stages);
        assert(cc_platform_capture_frame(platform, &frame));
        assert(count_partial_pixels(&frame) == 0);
        assert(cc_platform_set_antialiasing(platform, true));
        draw_rotated_texture(platform, texture, stages);
        assert(cc_platform_capture_frame(platform, &frame));
        assert(count_partial_pixels(&frame) > 100);
        assert(cc_platform_set_antialiasing(platform, false));
        draw_rotated_texture(platform, texture, stages);
        assert(cc_platform_capture_frame(platform, &frame));
        assert(count_partial_pixels(&frame) == 0);
    }
    cc_platform_capture_end(platform);
    cc_platform_destroy_texture(platform, texture);
}

static void test_composed_alpha(CcPlatform *platform, CcMetalState *state) {
    [state->view setFrameSize:NSMakeSize(80, 60)];
    CcFramebuffer frame = {0};
    assert(cc_platform_capture_begin(platform, &frame));
    const float alphas[] = {0.0f, 0.25f, 0.5f};
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        assert(cc_platform_set_antialiasing(platform, enabled != 0));
        for (unsigned index = 0; index < sizeof(alphas) / sizeof(alphas[0]); ++index) {
            CcMaterialQuad quad = sampling_quad(0, 0);
            for (unsigned vertex = 0; vertex < 4; ++vertex) {
                quad.vertices[vertex].x = vertex & 1 ? CC_FRAME_WIDTH : 0;
                quad.vertices[vertex].y = vertex & 2 ? CC_FRAME_HEIGHT : 0;
                quad.vertices[vertex].color = (CcColor){1, 0.25f, 0.5f, alphas[index]};
            }
            cc_platform_prepare_material(platform, &quad);
            cc_platform_begin(platform, (CcColor){0, 0, 0, 1});
            cc_platform_draw_material_quad(platform, &quad);
            cc_platform_end(platform);
            assert(cc_platform_capture_frame(platform, &frame));
            const uint8_t *pixel = frame.rgba +
                                   (size_t)(frame.height / 2) * frame.stride +
                                   (size_t)(frame.width / 2) * 4;
            assert(pixel[0] == 255 && pixel[1] == 64 && pixel[2] == 128);
            assert(abs((int)pixel[3] - (int)lroundf(alphas[index] * 255)) <= 1);
        }
    }
    cc_platform_capture_end(platform);
}

static void test_antialiasing(CcPlatform *platform, CcMetalState *state) {
    assert(!cc_platform_set_antialiasing(NULL, true));
    assert(cc_platform_set_antialiasing(platform, false));
    [state->view setFrameSize:NSMakeSize(80, 60)];
    CcFramebuffer frame = {0};
    assert(cc_platform_capture_begin(platform, &frame));
    int width = frame.width;
    int height = frame.height;
    draw_diagonal(platform);
    assert(cc_platform_capture_frame(platform, &frame));
    assert(count_partial_pixels(&frame) == 0);
    const uint8_t *capture_storage = frame.rgba;
    assert(cc_platform_set_antialiasing(platform, true));
    assert(state->antialias_samples == 2 || state->antialias_samples == 4);
    assert(state->antialias_pipeline);
    id<MTLTexture> colors[CC_IN_FLIGHT_FRAMES] = {nil};
    id<MTLTexture> depths[CC_IN_FLIGHT_FRAMES] = {nil};
    for (unsigned iteration = 0; iteration < CC_IN_FLIGHT_FRAMES * 2; ++iteration) {
        draw_diagonal(platform);
        assert(cc_platform_capture_frame(platform, &frame));
        assert(frame.width == width && frame.height == height &&
               frame.rgba == capture_storage);
        assert(count_partial_pixels(&frame) > 0);
        assert_capture_pixel(&frame, 0, 0, 0, 0, 0);
        NSUInteger slot = (state->frame_number - 1) % CC_IN_FLIGHT_FRAMES;
        assert(state->antialias_color[slot].sampleCount == state->antialias_samples);
        assert(state->antialias_depth[slot].sampleCount == state->antialias_samples);
        assert(state->antialias_color[slot].width == (NSUInteger)width);
        assert(state->antialias_color[slot].height == (NSUInteger)height);
        if (iteration < CC_IN_FLIGHT_FRAMES) {
            colors[slot] = state->antialias_color[slot];
            depths[slot] = state->antialias_depth[slot];
        } else {
            assert(colors[slot] == state->antialias_color[slot]);
            assert(depths[slot] == state->antialias_depth[slot]);
        }
    }

    /* Native logical-size previews must neither resolve nor replace the last
     * recorded window frame, even while presentation smoothing is enabled. */
    uint32_t preview = cc_platform_create_render_texture(platform);
    assert(preview &&
           cc_platform_begin_target(platform, preview, (CcColor){1, 0, 0, 1}));
    cc_platform_end(platform);
    cc_wait_for_metal(state);
    assert(cc_platform_capture_frame(platform, &frame) &&
           frame.rgba == capture_storage);
    assert(count_partial_pixels(&frame) > 0);
    id<MTLTexture> preview_texture = state->textures[preview];
    assert(preview_texture.sampleCount == 1 &&
           preview_texture.width == CC_FRAME_WIDTH &&
           preview_texture.height == CC_FRAME_HEIGHT);
    cc_platform_destroy_texture(platform, preview);

    /* Material preparation compiles the enabled sample count before drawing. */
    CcMaterialQuad material = sampling_quad(0, 1);
    material.blend_mode[0] = 1;
    material.blend_mode[1] = 1;
    material.blend_mode[2] = 1;
    cc_platform_prepare_material(platform, &material);
    assert(state->antialias_material_pipelines[1][9]);
    cc_platform_capture_end(platform);
    [state->view setFrameSize:NSMakeSize(97, 73)];
    assert(cc_platform_capture_begin(platform, &frame));
    draw_diagonal(platform);
    assert(cc_platform_capture_frame(platform, &frame));
    assert(frame.width != width && frame.height != height);
    NSUInteger slot = (state->frame_number - 1) % CC_IN_FLIGHT_FRAMES;
    assert(state->antialias_color[slot] != colors[slot]);
    assert(state->antialias_color[slot].width == (NSUInteger)frame.width);
    assert(state->antialias_color[slot].height == (NSUInteger)frame.height);
    assert(cc_platform_set_antialiasing(platform, false));
    for (unsigned index = 0; index < CC_IN_FLIGHT_FRAMES; ++index)
        assert(!state->antialias_color[index] && !state->antialias_depth[index]);
    draw_diagonal(platform);
    assert(cc_platform_capture_frame(platform, &frame));
    assert(count_partial_pixels(&frame) == 0);
    cc_platform_capture_end(platform);
}

int main(void) {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) {
            fputs("Metal unavailable; backend test skipped.\n", stderr);
            return 77;
        }
        CcMetalState *state = [CcMetalState new];
        assert(cc_prepare_metal(state));
        CcPlatform platform = {.metal_state = (__bridge void *)state};
        test_material_preparation(&platform, state);
        test_signed_quad(&platform, state);
        test_clip_rendering(&platform, state);
        test_material_sampling(&platform, state);
        test_framebuffer_capture(&platform, state);
        test_antialiasing(&platform, state);
        test_texture_sample_coverage(&platform, state);
        test_composed_alpha(&platform, state);
        cc_wait_for_metal(state);
        cc_release_metal(state);
    }
    return 0;
}
