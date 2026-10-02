#include "platform_metal_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geometry.h"
#include "material_blend.h"
#include "material_depth.h"
#include "shaders.h"
#include "console_common/render/viewport.h"

@implementation CcMetalState
@end

static CcMetalState *cc_state(CcPlatform *platform) {
    return platform ? (__bridge CcMetalState *)platform->metal_state : nil;
}

static void cc_release_retired_textures(CcMetalState *state) {
    NSUInteger handle = state->retired_texture_handles.firstIndex;
    while (handle != NSNotFound) {
        [state->textures replaceObjectAtIndex:handle withObject:[NSNull null]];
        [state->free_texture_handles addObject:@(handle)];
        handle = [state->retired_texture_handles indexGreaterThanIndex:handle];
    }
    [state->retired_texture_handles removeAllIndexes];
}

static id<MTLTexture> cc_make_texture(CcMetalState *state, int width, int height,
                                      const uint8_t *rgba) {
    if (width <= 0 || height <= 0 || !rgba || (size_t)width > SIZE_MAX / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4)) {
        return nil;
    }

    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    if (!texture) {
        return nil;
    }

    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);
    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:rgba
               bytesPerRow:(NSUInteger)width * 4];
    return texture;
}

/* Both uploaded images and render targets use the same reusable handle slots. */
static uint32_t cc_store_texture(CcMetalState *state, id<MTLTexture> texture) {
    if (state->free_texture_handles.count > 0) {
        NSNumber *available = state->free_texture_handles.lastObject;
        uint32_t handle = available.unsignedIntValue;
        [state->free_texture_handles removeLastObject];
        [state->textures replaceObjectAtIndex:handle withObject:texture];
        return handle;
    }
    uint32_t handle = (uint32_t)state->textures.count;
    [state->textures addObject:texture];
    return handle;
}

enum { CC_BLEND_DISABLED = 64, CC_BLEND_DEFAULT = 4 * 8 + 5 };

static id<MTLRenderPipelineState>
cc_make_pipeline(CcMetalState *state, id<MTLFunction> fragment, uint8_t blend_key) {
    static const MTLBlendFactor factors[8] = {
        MTLBlendFactorZero,
        MTLBlendFactorOne,
        MTLBlendFactorDestinationColor,
        MTLBlendFactorOneMinusDestinationColor,
        MTLBlendFactorSourceAlpha,
        MTLBlendFactorOneMinusSourceAlpha,
        MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha,
    };

    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.vertexFunction = state->vertex_function;
    description.fragmentFunction = fragment;
    description.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    MTLRenderPipelineColorAttachmentDescriptor *attachment =
        description.colorAttachments[0];
    attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
    attachment.blendingEnabled = blend_key != CC_BLEND_DISABLED;
    if (blend_key != CC_BLEND_DISABLED) {
        attachment.rgbBlendOperation = MTLBlendOperationAdd;
        attachment.alphaBlendOperation = MTLBlendOperationAdd;
        attachment.sourceRGBBlendFactor = factors[blend_key / 8];
        attachment.destinationRGBBlendFactor = factors[blend_key % 8];
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline =
        [state->device newRenderPipelineStateWithDescriptor:description error:&error];
    if (!pipeline) {
        const char *message = error.localizedDescription.UTF8String;
        fprintf(stderr, "Metal pipeline unavailable: %s\n",
                message ? message : "unknown error");
    }
    return pipeline;
}

static id<MTLRenderPipelineState>
cc_material_pipeline(CcMetalState *state, CcBatchKind kind, uint8_t blend_key) {
    NSNumber *key = @((NSUInteger)kind * 65u + blend_key);
    id<MTLRenderPipelineState> pipeline = state->material_pipelines[key];
    if (!pipeline) {
        /* Pipeline variants are cached; the shader library is compiled once. */
        id<MTLFunction> fragment = kind == CC_BATCH_TEV
                                       ? state->tev_fragment_function
                                       : state->material_fragment_function;
        pipeline = cc_make_pipeline(state, fragment, blend_key);
        if (pipeline) {
            state->material_pipelines[key] = pipeline;
        }
    }
    return pipeline;
}

static bool cc_prepare_depth_states(CcMetalState *state) {
    static const MTLCompareFunction comparisons[8] = {
        MTLCompareFunctionNever,        MTLCompareFunctionLess,
        MTLCompareFunctionEqual,        MTLCompareFunctionLessEqual,
        MTLCompareFunctionGreater,      MTLCompareFunctionNotEqual,
        MTLCompareFunctionGreaterEqual, MTLCompareFunctionAlways,
    };
    for (unsigned key = 0; key < 17; ++key) {
        MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];
        descriptor.depthCompareFunction =
            key ? comparisons[(key - 1) / 2] : MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = key && ((key - 1) & 1);
        state->depth_states[key] =
            [state->device newDepthStencilStateWithDescriptor:descriptor];
        if (!state->depth_states[key])
            return false;
    }
    return true;
}

static id<MTLTexture> cc_depth_texture(CcMetalState *state, NSUInteger slot,
                                       bool offscreen, NSUInteger width,
                                       NSUInteger height) {
    id<MTLTexture> texture =
        offscreen ? state->target_depth[slot] : state->window_depth[slot];
    if (texture && texture.width == width && texture.height == height)
        return texture;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:width
                                    height:height
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    texture = [state->device newTextureWithDescriptor:descriptor];
    if (offscreen)
        state->target_depth[slot] = texture;
    else
        state->window_depth[slot] = texture;
    return texture;
}

bool cc_prepare_metal(CcMetalState *state) {
    state->device = MTLCreateSystemDefaultDevice();
    if (!state->device) {
        fprintf(stderr, "Metal device unavailable.\n");
        return false;
    }

    state->command_queue = [state->device newCommandQueue];
    NSError *error = nil;
    id<MTLLibrary> library =
        [state->device newLibraryWithSource:cc_metal_shader_source()
                                    options:nil
                                      error:&error];
    if (!library) {
        const char *message = error.localizedDescription.UTF8String;
        fprintf(stderr, "Metal shader compilation failed: %s\n",
                message ? message : "unknown error");
        return false;
    }

    state->vertex_function = [library newFunctionWithName:@"cc_vertex"];
    id<MTLFunction> basic_fragment = [library newFunctionWithName:@"cc_fragment"];
    state->material_fragment_function =
        [library newFunctionWithName:@"cc_material_fragment"];
    state->tev_fragment_function = [library newFunctionWithName:@"cc_tev_fragment"];
    if (!state->vertex_function || !basic_fragment ||
        !state->material_fragment_function || !state->tev_fragment_function) {
        fprintf(stderr, "Metal shader entry point unavailable.\n");
        return false;
    }
    state->pipeline = cc_make_pipeline(state, basic_fragment, CC_BLEND_DEFAULT);
    state->material_pipelines = [NSMutableDictionary dictionary];
    if (!state->pipeline || !state->command_queue || !cc_prepare_depth_states(state)) {
        return false;
    }
    if (!cc_material_pipeline(state, CC_BATCH_MATERIAL, CC_BLEND_DEFAULT) ||
        !cc_material_pipeline(state, CC_BATCH_MATERIAL, CC_BLEND_DISABLED) ||
        !cc_material_pipeline(state, CC_BATCH_TEV, CC_BLEND_DEFAULT) ||
        !cc_material_pipeline(state, CC_BATCH_TEV, CC_BLEND_DISABLED)) {
        return false;
    }

    MTLSamplerDescriptor *sampler_description = [MTLSamplerDescriptor new];
    sampler_description.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_description.magFilter = MTLSamplerMinMagFilterLinear;
    const MTLSamplerAddressMode wraps[3] = {
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeMirrorRepeat,
    };
    for (unsigned s = 0; s < 3; ++s) {
        for (unsigned t = 0; t < 3; ++t) {
            sampler_description.sAddressMode = wraps[s];
            sampler_description.tAddressMode = wraps[t];
            state->samplers[s][t] =
                [state->device newSamplerStateWithDescriptor:sampler_description];
            if (!state->samplers[s][t]) {
                return false;
            }
        }
    }
    state->render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (!state->render_pass) {
        return false;
    }

    const uint8_t white[] = {255, 255, 255, 255};
    id<MTLTexture> white_texture = cc_make_texture(state, 1, 1, white);
    if (!white_texture) {
        return false;
    }
    state->textures = [NSMutableArray arrayWithObject:white_texture];
    state->free_texture_handles = [NSMutableArray array];
    state->retired_texture_handles = [NSMutableIndexSet indexSet];
    return true;
}

void cc_wait_for_metal(CcMetalState *state) {
    for (NSUInteger i = 0; i < CC_IN_FLIGHT_FRAMES; ++i) {
        [state->in_flight[i] waitUntilCompleted];
    }
}

void cc_release_metal(CcMetalState *state) {
    free(state->vertices);
    free(state->batches);
}

void cc_platform_begin(CcPlatform *platform, CcColor clear_color) {
    CcMetalState *state = cc_state(platform);
    if (!state) {
        return;
    }
    state->clear_color = clear_color;
    state->render_target_handle = 0;
    state->vertex_count = 0;
    state->batch_count = 0;
    /* A failed presentation may leave queued draws behind. The new frame
     * discards those draws before their texture handles become reusable. */
    cc_release_retired_textures(state);
    state->clip_enabled = false;
    CcQuad background = {.x = 0,
                         .y = 0,
                         .width = CC_FRAME_WIDTH,
                         .height = CC_FRAME_HEIGHT,
                         .u0 = 0,
                         .v0 = 0,
                         .u1 = 1,
                         .v1 = 1,
                         .color = clear_color};
    cc_platform_draw_quad(platform, &background);
}

void cc_platform_set_clip(CcPlatform *platform, const CcClipRect *rect) {
    CcMetalState *state = cc_state(platform);
    if (!state)
        return;
    state->clip_enabled = rect != NULL;
    if (rect)
        state->clip = *rect;
}

static bool cc_reserve_vertices(CcMetalState *state, size_t count) {
    if (count <= state->vertex_capacity) {
        return true;
    }
    size_t capacity = state->vertex_capacity ? state->vertex_capacity : 768;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return false;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(CcVertex)) {
        return false;
    }
    CcVertex *vertices = realloc(state->vertices, capacity * sizeof(CcVertex));
    if (!vertices) {
        return false;
    }
    state->vertices = vertices;
    state->vertex_capacity = capacity;
    return true;
}

static bool cc_reserve_batches(CcMetalState *state, size_t count) {
    if (count <= state->batch_capacity) {
        return true;
    }
    size_t capacity = state->batch_capacity ? state->batch_capacity : 64;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return false;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(CcBatch)) {
        return false;
    }
    CcBatch *batches = realloc(state->batches, capacity * sizeof(CcBatch));
    if (!batches) {
        return false;
    }
    state->batches = batches;
    state->batch_capacity = capacity;
    return true;
}

static CcVertex cc_vertex(float x, float y, float u, float v, CcColor color) {
    CcVertex vertex = {.x = x, .y = y, .clip_w = 1, .color = color};
    vertex.uv[0][0] = u;
    vertex.uv[0][1] = v;
    return vertex;
}

static uint32_t cc_resolve_texture(CcMetalState *state, uint32_t handle) {
    if ((NSUInteger)handle >= state->textures.count ||
        [state->retired_texture_handles containsIndex:handle] ||
        [state->textures objectAtIndex:handle] == [NSNull null]) {
        return 0;
    }
    return handle;
}

static bool cc_batch_states_equal(const CcBatchState *left, const CcBatchState *right) {
    if (left->kind != right->kind || left->clip_enabled != right->clip_enabled ||
        (left->clip_enabled &&
         memcmp(&left->clip, &right->clip, sizeof(left->clip)) != 0) ||
        memcmp(left->textures, right->textures, sizeof(left->textures)) != 0 ||
        memcmp(left->wrap_s, right->wrap_s, sizeof(left->wrap_s)) != 0 ||
        memcmp(left->wrap_t, right->wrap_t, sizeof(left->wrap_t)) != 0 ||
        left->blend_key != right->blend_key || left->depth_key != right->depth_key) {
        return false;
    }
    if (left->kind == CC_BATCH_MATERIAL) {
        return memcmp(&left->params.simple, &right->params.simple,
                      sizeof(left->params.simple)) == 0;
    }
    if (left->kind == CC_BATCH_TEV) {
        return memcmp(&left->params.tev, &right->params.tev,
                      sizeof(left->params.tev)) == 0;
    }
    return true;
}

static void cc_queue_quad(CcMetalState *state, const CcBatchState *batch_state,
                          const CcVertex corners[4]) {
    if (state->vertex_count > SIZE_MAX - CC_VERTICES_PER_QUAD) {
        return;
    }
    CcBatchState clipped_state = *batch_state;
    clipped_state.clip_enabled = state->clip_enabled;
    clipped_state.clip = state->clip;
    bool new_batch = state->batch_count == 0 ||
                     !cc_batch_states_equal(
                         &state->batches[state->batch_count - 1].state, &clipped_state);
    if (!cc_reserve_vertices(state, state->vertex_count + CC_VERTICES_PER_QUAD) ||
        (new_batch && !cc_reserve_batches(state, state->batch_count + 1))) {
        return;
    }

    CcVertex *vertices = state->vertices + state->vertex_count;
    for (size_t index = 0; index < CC_VERTICES_PER_QUAD; ++index) {
        vertices[index] = corners[cc_quad_triangle_order[index]];
    }
    if (new_batch) {
        state->batches[state->batch_count++] = (CcBatch){
            .state = clipped_state,
            .first_vertex = state->vertex_count,
            .vertex_count = CC_VERTICES_PER_QUAD,
        };
    } else {
        state->batches[state->batch_count - 1].vertex_count += CC_VERTICES_PER_QUAD;
    }
    state->vertex_count += CC_VERTICES_PER_QUAD;
}

void cc_platform_draw_quad(CcPlatform *platform, const CcQuad *quad) {
    if (!quad || quad->width <= 0 || quad->height <= 0) {
        return;
    }

    CcDrawVertex corners[CC_QUAD_CORNERS];
    cc_render_quad_corners(quad, corners);
    cc_platform_draw_vertices(platform, corners, quad->texture);
}

void cc_platform_draw_vertices(CcPlatform *platform, const CcDrawVertex corners[4],
                               uint32_t texture_handle) {
    CcMetalState *state = cc_state(platform);
    if (!state || !corners) {
        return;
    }

    CcVertex vertices[4];
    for (unsigned index = 0; index < 4; ++index) {
        const CcDrawVertex *corner = &corners[index];
        vertices[index] =
            cc_vertex(corner->x, corner->y, corner->u, corner->v, corner->color);
    }
    CcBatchState batch = {0};
    batch.kind = CC_BATCH_BASIC;
    batch.textures[0] = cc_resolve_texture(state, texture_handle);
    cc_queue_quad(state, &batch, vertices);
}

static bool cc_metal_tev_supported(CcMetalState *state, const CcMaterialQuad *quad) {
    if (quad->tev_stage_count == 0 || quad->tev_stage_count > 6) {
        return false;
    }
    bool invalid = quad->has_alpha_compare &&
                   ((quad->alpha_compare[0] & 15) > 7 ||
                    (quad->alpha_compare[0] >> 4) > 7 || quad->alpha_compare[1] > 3);
    for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
        const uint8_t *bytes = quad->tev_stages[stage];
        invalid = invalid || (bytes[8] & 15) > 7 || (bytes[8] >> 4) > 7 ||
                  (bytes[9] & 15) > 7 || (bytes[9] >> 4) > 7;
    }
    if (invalid && !state->warned_tev_encoding) {
        fprintf(stderr, "Metal: invalid TEV selector encoding uses the "
                        "simple material fallback.\n");
        state->warned_tev_encoding = true;
    }
    return !invalid;
}

void cc_platform_draw_material_quad(CcPlatform *platform, const CcMaterialQuad *quad) {
    CcMetalState *state = cc_state(platform);
    if (!state || !quad || quad->texture_count > CC_MATERIAL_TEXTURES) {
        return;
    }
    CcMaterialBlend blend;
    unsigned depth_key;
    if (!cc_material_blend_resolve(quad, &blend) ||
        !cc_material_depth_key(quad, &depth_key)) {
        return;
    }

    if (quad->tev_stage_count > 6 && !state->warned_tev_limit) {
        fprintf(stderr, "Metal: materials with over six TEV stages use the "
                        "simple material fallback.\n");
        state->warned_tev_limit = true;
    }

    CcVertex vertices[4] = {0};
    for (unsigned index = 0; index < 4; ++index) {
        vertices[index].x = quad->vertices[index].x;
        vertices[index].y = quad->vertices[index].y;
        vertices[index].depth = quad->vertices[index].depth;
        vertices[index].clip_w = cc_material_clip_w(&quad->vertices[index]);
        vertices[index].color = quad->vertices[index].color;
        memcpy(vertices[index].uv, quad->vertices[index].uv,
               sizeof(vertices[index].uv));
    }

    CcBatchState batch = {0};
    batch.kind = cc_metal_tev_supported(state, quad) ? CC_BATCH_TEV : CC_BATCH_MATERIAL;
    batch.depth_key = (uint8_t)depth_key;
    for (unsigned index = 0; index < CC_MATERIAL_TEXTURES; ++index) {
        batch.textures[index] = cc_resolve_texture(state, quad->textures[index]);
        batch.wrap_s[index] = quad->wrap_s[index] < 3 ? quad->wrap_s[index] : 0;
        batch.wrap_t[index] = quad->wrap_t[index] < 3 ? quad->wrap_t[index] : 0;
    }
    batch.blend_key = blend.enabled ? (uint8_t)(blend.source * 8 + blend.destination)
                                    : CC_BLEND_DISABLED;
    uint32_t comparisons = quad->has_alpha_compare ? quad->alpha_compare[0] : 0x77u;
    uint32_t operation = quad->has_alpha_compare ? quad->alpha_compare[1] : 0;
    uint32_t references = quad->has_alpha_compare
                              ? ((uint32_t)quad->alpha_compare[2] |
                                 ((uint32_t)quad->alpha_compare[3] << 8))
                              : 0;
    if (batch.kind == CC_BATCH_TEV) {
        memcpy(batch.params.tev.registers, quad->registers,
               sizeof(batch.params.tev.registers));
        memcpy(batch.params.tev.konst_colors, quad->konst_colors,
               sizeof(batch.params.tev.konst_colors));
        for (unsigned stage = 0; stage < quad->tev_stage_count; ++stage) {
            for (unsigned word = 0; word < 4; ++word) {
                uint32_t packed = 0;
                for (unsigned byte = 0; byte < 4; ++byte) {
                    packed |= (uint32_t)quad->tev_stages[stage][word * 4 + byte]
                              << (byte * 8);
                }
                batch.params.tev.stage_words[stage][word] = packed;
            }
        }
        for (unsigned index = 0; index < 4; ++index) {
            batch.params.tev.swap[index] = quad->tev_swap_table[index];
        }
        batch.params.tev.stage_count = quad->tev_stage_count;
        batch.params.tev.alpha_comparisons = comparisons;
        batch.params.tev.alpha_operation = operation;
        batch.params.tev.alpha_references = references;
    } else {
        memcpy(batch.params.simple.r0, quad->registers[0],
               sizeof(batch.params.simple.r0));
        memcpy(batch.params.simple.r1, quad->registers[1],
               sizeof(batch.params.simple.r1));
        memcpy(batch.params.simple.kc3, quad->konst_colors[3],
               sizeof(batch.params.simple.kc3));
        batch.params.simple.texture_count =
            quad->texture_count < 2 ? quad->texture_count : 2;
        batch.params.simple.alpha_comparisons = comparisons;
        batch.params.simple.alpha_operation = operation;
        batch.params.simple.alpha_references = references;
    }
    cc_queue_quad(state, &batch, vertices);
}

void cc_platform_prepare_material(CcPlatform *platform, const CcMaterialQuad *quad) {
    /* Both material fragment functions and stock blend pipelines are ready
     * when the Metal platform is created. */
    (void)platform;
    (void)quad;
}

static bool cc_upload_vertices(CcMetalState *state, NSUInteger slot) {
    if (state->vertex_count == 0) {
        return true;
    }
    if (state->vertex_count > SIZE_MAX / sizeof(CcVertex)) {
        return false;
    }

    size_t length = state->vertex_count * sizeof(CcVertex);
    if (length > NSUIntegerMax) {
        return false;
    }
    if (state->buffer_sizes[slot] < length) {
        NSUInteger capacity =
            state->buffer_sizes[slot] ? state->buffer_sizes[slot] : 65536;
        while (capacity < length) {
            if (capacity > NSUIntegerMax / 2) {
                capacity = (NSUInteger)length;
                break;
            }
            capacity *= 2;
        }
        id<MTLBuffer> buffer =
            [state->device newBufferWithLength:capacity
                                       options:MTLResourceStorageModeShared];
        if (!buffer) {
            return false;
        }
        state->vertex_buffers[slot] = buffer;
        state->buffer_sizes[slot] = capacity;
    }

    memcpy(state->vertex_buffers[slot].contents, state->vertices, length);
    return true;
}

static NSUInteger cc_scissor_coordinate(double value, NSUInteger limit) {
    if (value <= 0.0)
        return 0;
    if (value >= (double)limit)
        return limit;
    return (NSUInteger)value;
}

static MTLScissorRect cc_batch_scissor(const CcBatchState *batch, CcViewport viewport) {
    if (!batch->clip_enabled)
        return (MTLScissorRect){(NSUInteger)viewport.x, (NSUInteger)viewport.y,
                                (NSUInteger)viewport.width,
                                (NSUInteger)viewport.height};
    double scale_x = (double)viewport.width / CC_FRAME_WIDTH;
    double scale_y = (double)viewport.height / CC_FRAME_HEIGHT;
    double left = floor((double)batch->clip.x * scale_x);
    double top = floor((double)batch->clip.y * scale_y);
    double right = ceil((double)(batch->clip.x + batch->clip.width) * scale_x);
    double bottom = ceil((double)(batch->clip.y + batch->clip.height) * scale_y);
    NSUInteger x0 = cc_scissor_coordinate(left, (NSUInteger)viewport.width);
    NSUInteger y0 = cc_scissor_coordinate(top, (NSUInteger)viewport.height);
    NSUInteger x1 = cc_scissor_coordinate(right, (NSUInteger)viewport.width);
    NSUInteger y1 = cc_scissor_coordinate(bottom, (NSUInteger)viewport.height);
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    return (MTLScissorRect){x0 + (NSUInteger)viewport.x, y0 + (NSUInteger)viewport.y,
                            x1 - x0, y1 - y0};
}

static void cc_bind_batch_resources(CcMetalState *state,
                                    id<MTLRenderCommandEncoder> encoder,
                                    const CcBatchState *batch) {
    unsigned texture_count = batch->kind == CC_BATCH_TEV        ? 4u
                             : batch->kind == CC_BATCH_MATERIAL ? 2u
                                                                : 1u;
    for (unsigned slot_index = 0; slot_index < texture_count; ++slot_index) {
        uint32_t handle = batch->textures[slot_index];
        id entry = [state->textures objectAtIndex:handle];
        id<MTLTexture> texture =
            entry == [NSNull null] ? [state->textures objectAtIndex:0] : entry;
        [encoder setFragmentTexture:texture atIndex:slot_index];
        [encoder setFragmentSamplerState:state->samplers[batch->wrap_s[slot_index]]
                                                        [batch->wrap_t[slot_index]]
                                 atIndex:slot_index];
    }
    if (batch->kind == CC_BATCH_MATERIAL) {
        [encoder setFragmentBytes:&batch->params.simple
                           length:sizeof(batch->params.simple)
                          atIndex:0];
    } else if (batch->kind == CC_BATCH_TEV) {
        [encoder setFragmentBytes:&batch->params.tev
                           length:sizeof(batch->params.tev)
                          atIndex:0];
    }
}

static void cc_encode_batches(CcMetalState *state, id<MTLRenderCommandEncoder> encoder,
                              CcViewport content, NSUInteger slot) {
    /* The logical raster is anamorphic inside a centered 16:9 output. */
    MTLViewport viewport = {content.x,      content.y, content.width,
                            content.height, 0.0,       1.0};
    MTLScissorRect scissor = {(NSUInteger)content.x, (NSUInteger)content.y,
                              (NSUInteger)content.width, (NSUInteger)content.height};
    [encoder setViewport:viewport];
    [encoder setScissorRect:scissor];
    if (state->vertex_count == 0)
        return;
    [encoder setVertexBuffer:state->vertex_buffers[slot] offset:0 atIndex:0];
    id<MTLRenderPipelineState> bound_pipeline = nil;
    unsigned bound_depth = 17;
    for (size_t i = 0; i < state->batch_count; ++i) {
        const CcBatch *batch = &state->batches[i];
        MTLScissorRect batch_scissor = cc_batch_scissor(&batch->state, content);
        if (batch_scissor.width == 0 || batch_scissor.height == 0) {
            continue;
        }
        if (batch_scissor.x != scissor.x || batch_scissor.y != scissor.y ||
            batch_scissor.width != scissor.width ||
            batch_scissor.height != scissor.height) {
            [encoder setScissorRect:batch_scissor];
            scissor = batch_scissor;
        }
        CcBatchKind kind = batch->state.kind;
        id<MTLRenderPipelineState> pipeline =
            kind != CC_BATCH_BASIC
                ? cc_material_pipeline(state, kind, batch->state.blend_key)
                : state->pipeline;
        if (!pipeline) {
            continue;
        }
        if (pipeline != bound_pipeline) {
            [encoder setRenderPipelineState:pipeline];
            bound_pipeline = pipeline;
        }
        if (batch->state.depth_key != bound_depth) {
            [encoder setDepthStencilState:state->depth_states[batch->state.depth_key]];
            bound_depth = batch->state.depth_key;
        }
        cc_bind_batch_resources(state, encoder, &batch->state);
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:batch->first_vertex
                    vertexCount:batch->vertex_count];
    }
}

void cc_platform_end(CcPlatform *platform) {
    CcMetalState *state = cc_state(platform);
    if (!state || (!state->render_target_handle && !state->window.isVisible)) {
        return;
    }

    if (!state->render_target_handle && platform->fade_alpha > 0.0f) {
        cc_platform_set_clip(platform, NULL);
        CcQuad cover = {.x = 0,
                        .y = 0,
                        .width = CC_FRAME_WIDTH,
                        .height = CC_FRAME_HEIGHT,
                        .u0 = 0,
                        .v0 = 0,
                        .u1 = 1,
                        .v1 = 1,
                        .color = {0, 0, 0, platform->fade_alpha},
                        .texture = 0};
        cc_platform_draw_quad(platform, &cover);
    }

    @autoreleasepool {
        uint32_t target_handle = state->render_target_handle;
        state->render_target_handle = 0;
        id<MTLTexture> target = nil;
        if (target_handle > 0 && (NSUInteger)target_handle < state->textures.count) {
            id entry = [state->textures objectAtIndex:target_handle];
            if (entry != [NSNull null])
                target = entry;
        }
        NSSize size = [state->view convertSizeToBacking:state->view.bounds.size];
        NSUInteger drawable_width =
            target ? target.width : (NSUInteger)llround(size.width);
        NSUInteger drawable_height =
            target ? target.height : (NSUInteger)llround(size.height);
        if (drawable_width == 0 || drawable_height == 0) {
            return;
        }
        CcViewport content =
            target ? (CcViewport){0, 0, (int)drawable_width, (int)drawable_height}
                   : cc_viewport_fit((int)drawable_width, (int)drawable_height);
        if (!target) {
            state->layer.contentsScale = state->window.backingScaleFactor;
            CGSize drawable_size = CGSizeMake(drawable_width, drawable_height);
            if (!CGSizeEqualToSize(state->layer.drawableSize, drawable_size)) {
                state->layer.drawableSize = drawable_size;
            }
        }

        NSUInteger slot = state->frame_number % CC_IN_FLIGHT_FRAMES;
        [state->in_flight[slot] waitUntilCompleted];
        state->in_flight[slot] = nil;
        if (!cc_upload_vertices(state, slot)) {
            fprintf(stderr, "Metal vertex buffer allocation failed.\n");
            return;
        }
        id<MTLTexture> depth = cc_depth_texture(state, slot, target != nil,
                                                drawable_width, drawable_height);
        if (!depth) {
            fprintf(stderr, "Metal depth target allocation failed.\n");
            return;
        }

        id<CAMetalDrawable> drawable = target ? nil : [state->layer nextDrawable];
        if (!target && !drawable) {
            return;
        }
        id<MTLCommandBuffer> commands = [state->command_queue commandBuffer];
        if (!commands) {
            return;
        }

        MTLRenderPassColorAttachmentDescriptor *color =
            state->render_pass.colorAttachments[0];
        color.texture = target ? target : drawable.texture;
        color.loadAction = MTLLoadActionClear;
        color.storeAction = MTLStoreActionStore;
        color.clearColor =
            target ? MTLClearColorMake(state->clear_color.r, state->clear_color.g,
                                       state->clear_color.b, state->clear_color.a)
                   : MTLClearColorMake(0, 0, 0, 1);
        state->render_pass.depthAttachment.texture = depth;
        state->render_pass.depthAttachment.loadAction = MTLLoadActionClear;
        state->render_pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        state->render_pass.depthAttachment.clearDepth = 1.0;
        id<MTLRenderCommandEncoder> encoder =
            [commands renderCommandEncoderWithDescriptor:state->render_pass];
        color.texture = nil;
        state->render_pass.depthAttachment.texture = nil;
        if (!encoder) {
            return;
        }

        cc_encode_batches(state, encoder, content, slot);
        [encoder endEncoding];
        if (drawable)
            [commands presentDrawable:drawable];
        [commands commit];
        cc_release_retired_textures(state);
        state->in_flight[slot] = commands;
        state->frame_number++;
    }
}

void cc_platform_set_fade_alpha(CcPlatform *platform, float alpha) {
    if (platform)
        platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t cc_platform_create_render_texture(CcPlatform *platform) {
    CcMetalState *state = cc_state(platform);
    if (!state ||
        (state->free_texture_handles.count == 0 && state->textures.count >= UINT32_MAX))
        return 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:CC_FRAME_WIDTH
                                    height:CC_FRAME_HEIGHT
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    if (!texture)
        return 0;
    return cc_store_texture(state, texture);
}

bool cc_platform_begin_target(CcPlatform *platform, uint32_t texture,
                              CcColor clear_color) {
    CcMetalState *state = cc_state(platform);
    if (!state || texture == 0 || (NSUInteger)texture >= state->textures.count) {
        return false;
    }
    if ([state->retired_texture_handles containsIndex:texture]) {
        return false;
    }
    id entry = [state->textures objectAtIndex:texture];
    if (entry == [NSNull null] ||
        !([(id<MTLTexture>)entry usage] & MTLTextureUsageRenderTarget)) {
        return false;
    }
    cc_platform_begin(platform, clear_color);
    state->vertex_count = 0;
    state->batch_count = 0;
    state->render_target_handle = texture;
    return true;
}

uint32_t cc_platform_create_texture(CcPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
    CcMetalState *state = cc_state(platform);
    if (!state || (state->free_texture_handles.count == 0 &&
                   state->textures.count >= UINT32_MAX)) {
        return 0;
    }
    @autoreleasepool {
        id<MTLTexture> texture = cc_make_texture(state, width, height, rgba);
        if (!texture) {
            return 0;
        }
        return cc_store_texture(state, texture);
    }
}

void cc_platform_destroy_texture(CcPlatform *platform, uint32_t texture) {
    CcMetalState *state = cc_state(platform);
    if (!state || texture == 0 || (NSUInteger)texture >= state->textures.count) {
        return;
    }
    if ([state->retired_texture_handles containsIndex:texture] ||
        [state->textures objectAtIndex:texture] == [NSNull null]) {
        return;
    }
    if (state->render_target_handle == texture)
        state->render_target_handle = 0;
    /* Batches keep numeric handles until cc_platform_end encodes them. Keep
     * this object in its slot so earlier draws still use the original image. */
    [state->retired_texture_handles addIndex:texture];
}
