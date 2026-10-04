#include "platform_metal_internal.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geometry.h"
#include "clip.h"
#include "material_blend.h"
#include "material_depth.h"
#include "shaders.h"
#include "console_common/render/viewport.h"
#include "../framebuffer.h"

@implementation CcMetalState
@end

CcMetalState *cc_metal_platform_state(CcPlatform *platform) {
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

static bool cc_antialias_targets(CcMetalState *state, NSUInteger slot, NSUInteger width,
                                 NSUInteger height) {
    id<MTLTexture> color = state->antialias_color[slot];
    id<MTLTexture> depth = state->antialias_depth[slot];
    if (color && depth && color.width == width && color.height == height)
        return true;
    if (state->antialias_failed_width[slot] == width &&
        state->antialias_failed_height[slot] == height)
        return false;
    state->antialias_failed_width[slot] = width;
    state->antialias_failed_height[slot] = height;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2DMultisample;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.sampleCount = state->antialias_samples;
    descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    /* Tile GPUs can resolve coverage without storing the multisample surfaces
     * in main memory. Other Metal devices retain the private-storage path. */
    if (@available(macOS 11.0, *)) {
        if ([state->device supportsFamily:MTLGPUFamilyApple1])
            descriptor.storageMode = MTLStorageModeMemoryless;
    }
    color = [state->device newTextureWithDescriptor:descriptor];
    descriptor.pixelFormat = MTLPixelFormatDepth32Float;
    depth = [state->device newTextureWithDescriptor:descriptor];
    if (!color || !depth)
        return false;
    state->antialias_color[slot] = color;
    state->antialias_depth[slot] = depth;
    state->antialias_failed_width[slot] = 0;
    state->antialias_failed_height[slot] = 0;
    return true;
}

bool cc_platform_set_antialiasing(CcPlatform *platform, bool enabled) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state)
        return false;
    if (!enabled) {
        cc_wait_for_metal(state);
        state->antialiasing = false;
        for (NSUInteger slot = 0; slot < CC_IN_FLIGHT_FRAMES; ++slot) {
            state->antialias_color[slot] = nil;
            state->antialias_depth[slot] = nil;
            state->antialias_failed_width[slot] = 0;
            state->antialias_failed_height[slot] = 0;
        }
        return true;
    }
    state->antialias_samples = [state->device supportsTextureSampleCount:4]   ? 4
                               : [state->device supportsTextureSampleCount:2] ? 2
                                                                              : 0;
    if (!state->antialias_samples || !cc_metal_prepare_antialias_pipelines(state))
        return false;
    state->antialiasing = true;
    return true;
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
    if (!state->command_queue || !cc_metal_prepare_pipelines(state, basic_fragment)) {
        return false;
    }

    MTLSamplerDescriptor *sampler_description = [MTLSamplerDescriptor new];
    const MTLSamplerAddressMode wraps[3] = {
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeMirrorRepeat,
    };
    for (unsigned nearest = 0; nearest < 2; ++nearest) {
        MTLSamplerMinMagFilter filter =
            nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
        sampler_description.minFilter = sampler_description.magFilter = filter;
        for (unsigned s = 0; s < 3; ++s) {
            for (unsigned t = 0; t < 3; ++t) {
                sampler_description.sAddressMode = wraps[s];
                sampler_description.tAddressMode = wraps[t];
                state->samplers[nearest][s][t] =
                    [state->device newSamplerStateWithDescriptor:sampler_description];
                if (!state->samplers[nearest][s][t]) {
                    return false;
                }
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
    free(state->capture_rgba);
    state->capture_rgba = NULL;
    free(state->vertices);
    free(state->batches);
}

void cc_platform_begin(CcPlatform *platform, CcColor clear_color) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state) {
        return;
    }
    state->clear_color = clear_color;
    state->rendering_multisample = false;
    state->render_target_handle = 0;
    state->vertex_count = 0;
    state->batch_count = 0;
    /* A failed presentation may leave queued draws behind. The new frame
     * discards those draws before their texture handles become reusable. */
    cc_release_retired_textures(state);
    state->clip_enabled = false;
    /* Preserve the requested clear RGBA inside the content viewport. Blending
     * a translucent background over the opaque bars would change both RGB and A. */
    CcMaterialQuad background = {0};
    background.has_blend_mode = true;
    background.registers[1][0] = clear_color.r;
    background.registers[1][1] = clear_color.g;
    background.registers[1][2] = clear_color.b;
    background.registers[1][3] = clear_color.a;
    for (unsigned index = 0; index < CC_QUAD_CORNERS; ++index) {
        background.vertices[index].x = index & 1 ? CC_FRAME_WIDTH : 0;
        background.vertices[index].y = index & 2 ? CC_FRAME_HEIGHT : 0;
        background.vertices[index].color = (CcColor){1, 1, 1, 1};
    }
    cc_platform_draw_material_quad(platform, &background);
}

void cc_platform_set_clip(CcPlatform *platform, const CcClipRect *rect) {
    CcMetalState *state = cc_metal_platform_state(platform);
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
        memcmp(left->nearest, right->nearest, sizeof(left->nearest)) != 0 ||
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
    if (!cc_render_quad_has_area(quad)) {
        return;
    }

    CcDrawVertex corners[CC_QUAD_CORNERS];
    cc_render_quad_corners(quad, corners);
    cc_platform_draw_vertices(platform, corners, quad->texture);
}

void cc_platform_draw_vertices(CcPlatform *platform, const CcDrawVertex corners[4],
                               uint32_t texture_handle) {
    CcMetalState *state = cc_metal_platform_state(platform);
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
    CcTevSupport support = cc_material_tev_support(quad, true);
    if (support == CC_TEV_STAGE_LIMIT && !state->warned_tev_limit) {
        fprintf(stderr, "Metal: materials with over six TEV stages use the "
                        "simple material fallback.\n");
        state->warned_tev_limit = true;
    } else if (support == CC_TEV_INVALID_ENCODING && !state->warned_tev_encoding) {
        fprintf(stderr, "Metal: invalid TEV selector encoding uses the "
                        "simple material fallback.\n");
        state->warned_tev_encoding = true;
    }
    return support == CC_TEV_SUPPORTED;
}

void cc_platform_draw_material_quad(CcPlatform *platform, const CcMaterialQuad *quad) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state || !quad || quad->texture_count > CC_MATERIAL_TEXTURES) {
        return;
    }
    CcMaterialBlend blend;
    unsigned depth_key;
    if (!cc_material_blend_resolve(quad, &blend) ||
        !cc_material_depth_key(quad, &depth_key)) {
        return;
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
        batch.nearest[index] = quad->nearest[index];
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
    CcMetalState *state = cc_metal_platform_state(platform);
    CcMaterialBlend blend;
    unsigned depth_key;
    if (!state || !quad || quad->texture_count > CC_MATERIAL_TEXTURES ||
        !cc_material_blend_resolve(quad, &blend) ||
        !cc_material_depth_key(quad, &depth_key)) {
        return;
    }
    CcBatchKind kind =
        cc_metal_tev_supported(state, quad) ? CC_BATCH_TEV : CC_BATCH_MATERIAL;
    uint8_t blend_key = blend.enabled ? (uint8_t)(blend.source * 8 + blend.destination)
                                      : CC_BLEND_DISABLED;
    (void)cc_metal_material_pipeline(state, kind, blend_key);
    if (state->antialiasing) {
        bool previous = state->rendering_multisample;
        state->rendering_multisample = true;
        (void)cc_metal_material_pipeline(state, kind, blend_key);
        state->rendering_multisample = previous;
    }
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

static MTLScissorRect cc_batch_scissor(const CcBatchState *batch, CcViewport viewport) {
    const CcClipRect *rect = batch->clip_enabled ? &batch->clip : NULL;
    CcViewport clip = cc_render_clip_pixels(rect, viewport.width, viewport.height);
    return (MTLScissorRect){(NSUInteger)(viewport.x + clip.x),
                            (NSUInteger)(viewport.y + clip.y), (NSUInteger)clip.width,
                            (NSUInteger)clip.height};
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
        [encoder setFragmentSamplerState:state->samplers[batch->nearest[slot_index]]
                                                        [batch->wrap_s[slot_index]]
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
                ? cc_metal_material_pipeline(state, kind, batch->state.blend_key)
            : state->rendering_multisample ? state->antialias_pipeline
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

static bool cc_encode_readback(CcMetalState *state, id<MTLCommandBuffer> commands) {
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    if (!blit)
        return false;
    [blit copyFromTexture:state->capture_texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake((NSUInteger)state->capture_width,
                                             (NSUInteger)state->capture_height, 1)
                        toBuffer:state->capture_readback
               destinationOffset:0
          destinationBytesPerRow:state->capture_stride
        destinationBytesPerImage:state->capture_readback.length];
    [blit endEncoding];
    return true;
}

static bool cc_encode_capture_presentation(CcMetalState *state,
                                           id<MTLCommandBuffer> commands,
                                           id<MTLTexture> drawable, NSUInteger slot) {
    id<MTLTexture> depth =
        cc_depth_texture(state, slot, true, drawable.width, drawable.height);
    if (!depth)
        return false;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1;
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return false;
    CcViewport fit = cc_viewport_fit((int)drawable.width, (int)drawable.height);
    CcViewport source = cc_viewport_fit(state->capture_width, state->capture_height);
    float u0 = (float)source.x / (float)state->capture_width;
    float v0 = (float)source.y / (float)state->capture_height;
    float u1 = (float)(source.x + source.width) / (float)state->capture_width;
    float v1 = (float)(source.y + source.height) / (float)state->capture_height;
    [encoder setViewport:(MTLViewport){fit.x, fit.y, fit.width, fit.height, 0, 1}];
    [encoder
        setScissorRect:(MTLScissorRect){(NSUInteger)fit.x, (NSUInteger)fit.y,
                                        (NSUInteger)fit.width, (NSUInteger)fit.height}];
    CcVertex vertices[CC_VERTICES_PER_QUAD];
    const CcColor white = {1, 1, 1, 1};
    const CcVertex corners[] = {
        cc_vertex(0, 0, u0, v0, white),
        cc_vertex(CC_FRAME_WIDTH, 0, u1, v0, white),
        cc_vertex(0, CC_FRAME_HEIGHT, u0, v1, white),
        cc_vertex(CC_FRAME_WIDTH, CC_FRAME_HEIGHT, u1, v1, white),
    };
    for (unsigned i = 0; i < CC_VERTICES_PER_QUAD; ++i)
        vertices[i] = corners[cc_quad_triangle_order[i]];
    /* A copied constant buffer leaves the scene's in-flight vertex data intact. */
    [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
    /* Copy the completed framebuffer without blending its alpha a second time. */
    [encoder setRenderPipelineState:state->presentation_pipeline];
    [encoder setDepthStencilState:state->depth_states[0]];
    [encoder setFragmentTexture:state->capture_texture atIndex:0];
    [encoder setFragmentSamplerState:state->samplers[0][0][0] atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:CC_VERTICES_PER_QUAD];
    [encoder endEncoding];
    return true;
}

void cc_platform_end(CcPlatform *platform) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (state && !state->render_target_handle)
        state->capture_ready = false;
    if (!state || (!state->render_target_handle && !state->capture_texture &&
                   !state->window.isVisible)) {
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
        bool capturing = !target && state->capture_texture != nil;
        NSSize size = [state->view convertSizeToBacking:state->view.bounds.size];
        NSUInteger drawable_width = target      ? target.width
                                    : capturing ? (NSUInteger)state->capture_width
                                                : (NSUInteger)llround(size.width);
        NSUInteger drawable_height = target      ? target.height
                                     : capturing ? (NSUInteger)state->capture_height
                                                 : (NSUInteger)llround(size.height);
        if (drawable_width == 0 || drawable_height == 0) {
            return;
        }
        CcViewport content =
            target ? (CcViewport){0, 0, (int)drawable_width, (int)drawable_height}
                   : cc_viewport_fit((int)drawable_width, (int)drawable_height);
        if (!target) {
            state->layer.contentsScale = state->window.backingScaleFactor;
            CGSize drawable_size =
                CGSizeMake(fmax(1.0, round(size.width)), fmax(1.0, round(size.height)));
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
        state->rendering_multisample =
            !target && state->antialiasing &&
            cc_antialias_targets(state, slot, drawable_width, drawable_height);
        if (!target && state->antialiasing && !state->rendering_multisample &&
            !state->warned_antialiasing) {
            fprintf(stderr,
                    "Metal: antialias target unavailable; using ordinary rendering.\n");
            state->warned_antialiasing = true;
        }
        id<MTLTexture> depth = state->rendering_multisample
                                   ? state->antialias_depth[slot]
                                   : cc_depth_texture(state, slot, target != nil,
                                                      drawable_width, drawable_height);
        if (!depth) {
            fprintf(stderr, "Metal depth target allocation failed.\n");
            return;
        }

        id<CAMetalDrawable> drawable = target ? nil : [state->layer nextDrawable];
        if (!target && !capturing && !drawable) {
            return;
        }
        id<MTLCommandBuffer> commands = [state->command_queue commandBuffer];
        if (!commands) {
            return;
        }

        MTLRenderPassColorAttachmentDescriptor *color =
            state->render_pass.colorAttachments[0];
        id<MTLTexture> output = target      ? target
                                : capturing ? state->capture_texture
                                            : drawable.texture;
        color.texture =
            state->rendering_multisample ? state->antialias_color[slot] : output;
        color.resolveTexture = state->rendering_multisample ? output : nil;
        color.loadAction = MTLLoadActionClear;
        color.storeAction = state->rendering_multisample
                                ? MTLStoreActionMultisampleResolve
                                : MTLStoreActionStore;
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
        color.resolveTexture = nil;
        state->render_pass.depthAttachment.texture = nil;
        if (!encoder) {
            return;
        }

        cc_encode_batches(state, encoder, content, slot);
        [encoder endEncoding];
        state->rendering_multisample = false;
        bool readback_encoded = capturing && cc_encode_readback(state, commands);
        if (capturing && drawable &&
            !cc_encode_capture_presentation(state, commands, drawable.texture, slot))
            drawable = nil;
        if (drawable)
            [commands presentDrawable:drawable];
        [commands commit];
        cc_release_retired_textures(state);
        state->in_flight[slot] = commands;
        state->frame_number++;
        if (readback_encoded) {
            [commands waitUntilCompleted];
            state->capture_ready =
                commands.status == MTLCommandBufferStatusCompleted &&
                cc_framebuffer_rgba(state->capture_rgba, state->capture_byte_count,
                                    state->capture_readback.contents,
                                    state->capture_width, state->capture_height,
                                    state->capture_stride, true, false);
        }
    }
}

bool cc_platform_capture_begin(CcPlatform *platform, CcFramebuffer *frame) {
    if (!frame)
        return false;
    *frame = (CcFramebuffer){0};
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state || state->capture_texture || !state->view)
        return false;
    NSSize size = [state->view convertSizeToBacking:state->view.bounds.size];
    if (!isfinite(size.width) || !isfinite(size.height) || size.width < 1 ||
        size.height < 1 || size.width > INT_MAX || size.height > INT_MAX)
        return false;
    int width = (int)llround(size.width);
    int height = (int)llround(size.height);
    size_t row = 0;
    size_t byte_count = 0;
    size_t aligned_row = 0;
    size_t aligned_bytes = 0;
    if (!cc_framebuffer_storage(width, height, 1, &row, &byte_count) ||
        !cc_framebuffer_storage(width, height, 256, &aligned_row, &aligned_bytes))
        return false;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [state->device newTextureWithDescriptor:descriptor];
    id<MTLBuffer> readback =
        [state->device newBufferWithLength:aligned_bytes
                                   options:MTLResourceStorageModeShared];
    uint8_t *rgba = texture && readback ? malloc(byte_count) : NULL;
    if (!rgba)
        return false;
    cc_wait_for_metal(state);
    state->capture_texture = texture;
    state->capture_readback = readback;
    state->capture_rgba = rgba;
    state->capture_stride = aligned_row;
    state->capture_byte_count = byte_count;
    state->capture_width = width;
    state->capture_height = height;
    state->capture_ready = false;
    *frame = (CcFramebuffer){NULL, width, height, row};
    return true;
}

bool cc_platform_capture_frame(CcPlatform *platform, CcFramebuffer *frame) {
    if (!frame)
        return false;
    *frame = (CcFramebuffer){0};
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state || !state->capture_ready)
        return false;
    *frame = (CcFramebuffer){state->capture_rgba, state->capture_width,
                             state->capture_height, (size_t)state->capture_width * 4};
    return true;
}

void cc_platform_capture_end(CcPlatform *platform) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state)
        return;
    cc_wait_for_metal(state);
    state->capture_texture = nil;
    state->capture_readback = nil;
    free(state->capture_rgba);
    state->capture_rgba = NULL;
    state->capture_stride = 0;
    state->capture_byte_count = 0;
    state->capture_width = 0;
    state->capture_height = 0;
    state->capture_ready = false;
}

void cc_platform_set_fade_alpha(CcPlatform *platform, float alpha) {
    if (platform)
        platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t cc_platform_create_render_texture(CcPlatform *platform) {
    CcMetalState *state = cc_metal_platform_state(platform);
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
    CcMetalState *state = cc_metal_platform_state(platform);
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
    CcMetalState *state = cc_metal_platform_state(platform);
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
    CcMetalState *state = cc_metal_platform_state(platform);
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
