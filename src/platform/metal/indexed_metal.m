#include "console_common/platform/indexed.h"

#include "console_common/render/viewport.h"
#include "console_common/support/error.h"
#include "indexed.h"
#include "platform_metal_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CC_INDEXED_UNIFORM_BYTES = CC_INDEXED_UNIFORMS * 4 * sizeof(float) };

@interface CcIndexedMetalProgram : NSObject
@property(nonatomic, strong) id<MTLRenderPipelineState> colorPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> depthPipeline;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLDepthStencilState> disabledDepthState;
@end
@implementation CcIndexedMetalProgram
@end

@interface CcIndexedMetalMesh : NSObject {
  @public
    id<MTLBuffer> frameVertices[CC_IN_FLIGHT_FRAMES];
}
@property(nonatomic, strong) id<MTLBuffer> vertices;
@property(nonatomic, strong) id<MTLBuffer> indices;
@end
@implementation CcIndexedMetalMesh
@end

@interface CcIndexedMetalTexture : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic, strong) id<MTLSamplerState> sampler;
@end
@implementation CcIndexedMetalTexture
@end

@interface CcIndexedMetalFrames : NSObject {
  @public
    id<MTLBuffer> uniforms[CC_IN_FLIGHT_FRAMES];
    id<MTLCommandBuffer> commands[CC_IN_FLIGHT_FRAMES];
    id<MTLTexture> depth[CC_IN_FLIGHT_FRAMES];
}
@property(nonatomic, strong) id<CAMetalDrawable> drawable;
@property(nonatomic, strong) MTLRenderPassDescriptor *pass;
@end
@implementation CcIndexedMetalFrames
@end

struct CcIndexedProgram {
    CcIndexedRenderer *owner;
    CcIndexedProgram *next;
    void *objects;
    size_t vertex_stride;
    size_t vertex_uniform_count;
    size_t fragment_uniform_count;
    size_t texture_count;
    unsigned vertex_uniform_buffer;
    unsigned fragment_uniform_buffer;
    CcIndexedState state;
};

struct CcIndexedMesh {
    CcIndexedRenderer *owner;
    CcIndexedMesh *next;
    void *objects;
    size_t vertex_stride;
    size_t vertex_bytes;
    size_t index_count;
    uint8_t *staging;
    bool dirty[CC_IN_FLIGHT_FRAMES];
};

struct CcIndexedTexture {
    CcIndexedRenderer *owner;
    CcIndexedTexture *next;
    void *objects;
};

typedef struct CcIndexedMetalDraw {
    CcIndexedProgram *program;
    CcIndexedMesh *mesh;
    CcIndexedTexture *textures[CC_INDEXED_TEXTURES];
    size_t texture_count;
    size_t first_index;
    size_t index_count;
} CcIndexedMetalDraw;

struct CcIndexedRenderer {
    CcPlatform *platform;
    void *objects;
    CcIndexedProgram *programs;
    CcIndexedMesh *meshes;
    CcIndexedTexture *textures;
    CcIndexedMetalDraw *draws;
    size_t capacity;
    size_t draw_count;
    CcIndexedFrame frame;
    CcViewport viewport;
    uint64_t frame_number;
    bool active;
};

static bool fail(char *error, size_t capacity, const char *message) {
    cc_error_set(error, capacity, message);
    return false;
}

static bool metal_error(char *error, size_t capacity, NSError *diagnostic,
                        const char *message) {
    const char *detail = diagnostic.localizedDescription.UTF8String;
    return fail(error, capacity, detail ? detail : message);
}

static CcMetalState *native_state(CcIndexedRenderer *renderer) {
    return renderer ? cc_metal_platform_state(renderer->platform) : nil;
}

static bool prepare_resources(CcIndexedRenderer *renderer, char *error,
                              size_t error_capacity) {
    CcMetalState *state = native_state(renderer);
    if (!renderer || renderer->active || !state || !state->device)
        return fail(error, error_capacity, "indexed Metal resource state unavailable");
    return true;
}

static CcIndexedRenderer *create_renderer(CcPlatform *platform, char *error,
                                          size_t error_capacity) {
    CcMetalState *state = cc_metal_platform_state(platform);
    if (!state || !state->device || !state->command_queue || !state->layer) {
        fail(error, error_capacity, "indexed native Metal host unavailable");
        return NULL;
    }
    CcIndexedRenderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) {
        fail(error, error_capacity, "cannot allocate indexed renderer");
        return NULL;
    }
    CcIndexedMetalFrames *frames = [CcIndexedMetalFrames new];
    frames.pass = [MTLRenderPassDescriptor renderPassDescriptor];
    renderer->platform = platform;
    renderer->objects = (__bridge_retained void *)frames;
    return renderer;
}

static void wait_for_frames(CcIndexedRenderer *renderer) {
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index) {
        [frames->commands[index] waitUntilCompleted];
        frames->commands[index] = nil;
    }
}

void cc_indexed_destroy(CcIndexedRenderer *renderer) {
    if (!renderer)
        return;
    wait_for_frames(renderer);
    while (renderer->programs) {
        CcIndexedProgram *program = renderer->programs;
        renderer->programs = program->next;
        (void)CFBridgingRelease(program->objects);
        free(program);
    }
    while (renderer->meshes) {
        CcIndexedMesh *mesh = renderer->meshes;
        renderer->meshes = mesh->next;
        (void)CFBridgingRelease(mesh->objects);
        free(mesh->staging);
        free(mesh);
    }
    while (renderer->textures) {
        CcIndexedTexture *texture = renderer->textures;
        renderer->textures = texture->next;
        (void)CFBridgingRelease(texture->objects);
        free(texture);
    }
    (void)CFBridgingRelease(renderer->objects);
    free(renderer->draws);
    free(renderer);
}

static bool reserve_draws(CcIndexedRenderer *renderer, size_t draw_count, char *error,
                          size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    if (draw_count > SIZE_MAX / sizeof(CcIndexedMetalDraw) ||
        draw_count > SIZE_MAX / (CC_INDEXED_UNIFORM_BYTES * 2))
        return fail(error, error_capacity, "indexed draw capacity overflows storage");
    if (draw_count <= renderer->capacity)
        return true;
    size_t byte_count = draw_count * CC_INDEXED_UNIFORM_BYTES * 2;
    CcMetalState *state = native_state(renderer);
    if (byte_count > state->device.maxBufferLength)
        return fail(error, error_capacity,
                    "indexed uniform storage exceeds Metal limit");
    CcIndexedMetalDraw *draws = calloc(draw_count, sizeof(*draws));
    if (!draws)
        return fail(error, error_capacity, "cannot allocate indexed draw storage");
    id<MTLBuffer> buffers[CC_IN_FLIGHT_FRAMES];
    for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index) {
        buffers[index] =
            [state->device newBufferWithLength:byte_count
                                       options:MTLResourceStorageModeShared];
        if (!buffers[index]) {
            free(draws);
            return fail(error, error_capacity,
                        "cannot reserve indexed uniform buffers");
        }
    }
    wait_for_frames(renderer);
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index)
        frames->uniforms[index] = buffers[index];
    free(renderer->draws);
    renderer->draws = draws;
    renderer->capacity = draw_count;
    return true;
}

static MTLCompileOptions *precise_compile_options(void) {
    MTLCompileOptions *options = [MTLCompileOptions new];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
        return options;
    }
#endif
    /* Earlier macOS versions expose the same precision policy through this
     * deprecated property. Keep the warning exception at that fallback. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    return options;
}

static id<MTLFunction> compile_function(id<MTLDevice> device, const char *source,
                                        const char *entry, char *error,
                                        size_t error_capacity) {
    if (!source || !source[0] || !entry || !entry[0]) {
        fail(error, error_capacity, "missing indexed Metal source or entry");
        return nil;
    }
    NSString *text = [NSString stringWithUTF8String:source];
    NSString *name = [NSString stringWithUTF8String:entry];
    if (!text || !name) {
        fail(error, error_capacity, "invalid indexed Metal source encoding");
        return nil;
    }
    NSError *diagnostic = nil;
    MTLCompileOptions *options = precise_compile_options();
    id<MTLLibrary> library = [device newLibraryWithSource:text
                                                  options:options
                                                    error:&diagnostic];
    if (!library) {
        metal_error(error, error_capacity, diagnostic,
                    "cannot compile indexed Metal source");
        return nil;
    }
    id<MTLFunction> function = [library newFunctionWithName:name];
    if (!function)
        fail(error, error_capacity, "indexed Metal entry is unavailable");
    return function;
}

static MTLBlendFactor blend_factor(CcIndexedBlendFactor factor) {
    static const MTLBlendFactor factors[] = {MTLBlendFactorZero,
                                             MTLBlendFactorOne,
                                             MTLBlendFactorSourceColor,
                                             MTLBlendFactorOneMinusSourceColor,
                                             MTLBlendFactorDestinationColor,
                                             MTLBlendFactorOneMinusDestinationColor,
                                             MTLBlendFactorSourceAlpha,
                                             MTLBlendFactorOneMinusSourceAlpha,
                                             MTLBlendFactorDestinationAlpha,
                                             MTLBlendFactorOneMinusDestinationAlpha,
                                             MTLBlendFactorBlendColor};
    return factors[factor];
}

static MTLBlendOperation blend_operation(CcIndexedBlendEquation equation) {
    static const MTLBlendOperation operations[] = {MTLBlendOperationAdd,
                                                   MTLBlendOperationSubtract,
                                                   MTLBlendOperationReverseSubtract};
    return operations[equation];
}

static MTLRenderPipelineDescriptor *
pipeline_description(CcMetalState *native,
                     const CcIndexedProgramDescription *description,
                     id<MTLFunction> vertex, id<MTLFunction> fragment) {
    MTLRenderPipelineDescriptor *pipeline = [MTLRenderPipelineDescriptor new];
    pipeline.vertexFunction = vertex;
    pipeline.fragmentFunction = fragment;
    MTLVertexDescriptor *layout = [MTLVertexDescriptor vertexDescriptor];
    layout.layouts[0].stride = description->vertex_stride;
    layout.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    layout.layouts[0].stepRate = 1;
    for (size_t index = 0; index < description->attribute_count; ++index) {
        layout.attributes[index].format = MTLVertexFormatFloat4;
        layout.attributes[index].offset = description->attributes[index].offset;
        layout.attributes[index].bufferIndex = 0;
    }
    pipeline.vertexDescriptor = layout;
    const CcIndexedState *state = &description->state;
    MTLRenderPipelineColorAttachmentDescriptor *color = pipeline.colorAttachments[0];
    color.pixelFormat = native->layer.pixelFormat;
    color.blendingEnabled = state->blend;
    color.rgbBlendOperation = blend_operation(state->equation_rgb);
    color.alphaBlendOperation = blend_operation(state->equation_alpha);
    color.sourceRGBBlendFactor = blend_factor(state->source_rgb);
    color.destinationRGBBlendFactor = blend_factor(state->destination_rgb);
    color.sourceAlphaBlendFactor = blend_factor(state->source_alpha);
    color.destinationAlphaBlendFactor = blend_factor(state->destination_alpha);
    color.writeMask = MTLColorWriteMaskNone;
    static const MTLColorWriteMask masks[] = {
        MTLColorWriteMaskRed, MTLColorWriteMaskGreen, MTLColorWriteMaskBlue,
        MTLColorWriteMaskAlpha};
    for (size_t index = 0; index < 4; ++index) {
        if (state->color_write[index])
            color.writeMask |= masks[index];
    }
    return pipeline;
}

static CcIndexedProgram *create_program(CcIndexedRenderer *renderer,
                                        const CcIndexedProgramDescription *description,
                                        char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_program_validate(description, error, error_capacity))
        return NULL;
    CcMetalState *native = native_state(renderer);
    id<MTLFunction> vertex =
        compile_function(native->device, description->metal_vertex_source,
                         description->metal_vertex_entry, error, error_capacity);
    if (!vertex)
        return NULL;
    id<MTLFunction> fragment =
        compile_function(native->device, description->metal_fragment_source,
                         description->metal_fragment_entry, error, error_capacity);
    if (!fragment)
        return NULL;
    MTLRenderPipelineDescriptor *pipeline =
        pipeline_description(native, description, vertex, fragment);
    CcIndexedMetalProgram *objects = [CcIndexedMetalProgram new];
    NSError *diagnostic = nil;
    objects.colorPipeline =
        [native->device newRenderPipelineStateWithDescriptor:pipeline
                                                       error:&diagnostic];
    if (!objects.colorPipeline) {
        metal_error(error, error_capacity, diagnostic,
                    "invalid indexed Metal pipeline");
        return NULL;
    }
    pipeline.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    objects.depthPipeline =
        [native->device newRenderPipelineStateWithDescriptor:pipeline
                                                       error:&diagnostic];
    MTLDepthStencilDescriptor *depth = [MTLDepthStencilDescriptor new];
    depth.depthWriteEnabled =
        description->state.depth_write && description->state.depth_test;
    depth.depthCompareFunction =
        description->state.depth_test
            ? (MTLCompareFunction)description->state.depth_compare
            : MTLCompareFunctionAlways;
    objects.depthState = [native->device newDepthStencilStateWithDescriptor:depth];
    depth.depthWriteEnabled = NO;
    depth.depthCompareFunction = MTLCompareFunctionAlways;
    objects.disabledDepthState =
        [native->device newDepthStencilStateWithDescriptor:depth];
    if (!objects.depthPipeline || !objects.depthState || !objects.disabledDepthState) {
        metal_error(error, error_capacity, diagnostic,
                    "cannot create indexed Metal depth state");
        return NULL;
    }
    CcIndexedProgram *program = calloc(1, sizeof(*program));
    if (!program) {
        fail(error, error_capacity, "cannot allocate indexed program");
        return NULL;
    }
    program->owner = renderer;
    program->objects = (__bridge_retained void *)objects;
    program->state = description->state;
    program->vertex_stride = description->vertex_stride;
    program->vertex_uniform_count = description->vertex_uniform_count;
    program->fragment_uniform_count = description->fragment_uniform_count;
    program->texture_count = description->texture_count;
    program->vertex_uniform_buffer = description->metal_vertex_uniform_buffer;
    program->fragment_uniform_buffer = description->metal_fragment_uniform_buffer;
    program->next = renderer->programs;
    renderer->programs = program;
    return program;
}

static CcIndexedMesh *create_mesh(CcIndexedRenderer *renderer,
                                  const CcIndexedMeshDescription *description,
                                  char *error, size_t error_capacity,
                                  bool dynamic_vertices) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_mesh_validate(description, error, error_capacity))
        return NULL;
    CcMetalState *native = native_state(renderer);
    size_t vertex_bytes = description->vertex_count * description->vertex_stride;
    size_t index_bytes = description->index_count * sizeof(uint16_t);
    if (vertex_bytes > native->device.maxBufferLength ||
        index_bytes > native->device.maxBufferLength) {
        fail(error, error_capacity, "indexed mesh exceeds Metal buffer limit");
        return NULL;
    }
    CcIndexedMetalMesh *objects = [CcIndexedMetalMesh new];
    if (dynamic_vertices) {
        for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index) {
            objects->frameVertices[index] =
                [native->device newBufferWithBytes:description->vertices
                                            length:vertex_bytes
                                           options:MTLResourceStorageModeShared];
            if (!objects->frameVertices[index]) {
                fail(error, error_capacity, "cannot allocate indexed dynamic vertices");
                return NULL;
            }
        }
    } else {
        objects.vertices =
            [native->device newBufferWithBytes:description->vertices
                                        length:vertex_bytes
                                       options:MTLResourceStorageModeShared];
    }
    objects.indices = [native->device newBufferWithBytes:description->indices
                                                  length:index_bytes
                                                 options:MTLResourceStorageModeShared];
    if ((!dynamic_vertices && !objects.vertices) || !objects.indices) {
        fail(error, error_capacity, "cannot upload indexed Metal mesh");
        return NULL;
    }
    CcIndexedMesh *mesh = calloc(1, sizeof(*mesh));
    if (!mesh) {
        fail(error, error_capacity, "cannot allocate indexed mesh");
        return NULL;
    }
    if (dynamic_vertices) {
        mesh->staging = malloc(vertex_bytes);
        if (!mesh->staging) {
            free(mesh);
            fail(error, error_capacity, "cannot allocate indexed vertex staging");
            return NULL;
        }
        memcpy(mesh->staging, description->vertices, vertex_bytes);
    }
    mesh->owner = renderer;
    mesh->objects = (__bridge_retained void *)objects;
    mesh->vertex_stride = description->vertex_stride;
    mesh->vertex_bytes = vertex_bytes;
    mesh->index_count = description->index_count;
    mesh->next = renderer->meshes;
    renderer->meshes = mesh;
    return mesh;
}

static CcIndexedTexture *create_texture(CcIndexedRenderer *renderer,
                                        const CcIndexedTextureDescription *description,
                                        char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_texture_validate(description, error, error_capacity))
        return NULL;
    if (description->levels[0].width > 16384 || description->levels[0].height > 16384) {
        fail(error, error_capacity, "indexed texture exceeds Metal 2D limit");
        return NULL;
    }
    CcMetalState *native = native_state(renderer);
    MTLTextureDescriptor *layout = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:description->levels[0].width
                                    height:description->levels[0].height
                                 mipmapped:description->level_count > 1];
    layout.mipmapLevelCount = description->level_count;
    layout.storageMode = MTLStorageModeShared;
    layout.usage = MTLTextureUsageShaderRead;
    CcIndexedMetalTexture *objects = [CcIndexedMetalTexture new];
    objects.texture = [native->device newTextureWithDescriptor:layout];
    if (!objects.texture) {
        fail(error, error_capacity, "cannot allocate indexed Metal texture");
        return NULL;
    }
    for (size_t index = 0; index < description->level_count; ++index) {
        const CcIndexedMip *level = &description->levels[index];
        MTLRegion region = MTLRegionMake2D(0, 0, level->width, level->height);
        [objects.texture replaceRegion:region
                           mipmapLevel:index
                             withBytes:level->rgba
                           bytesPerRow:(size_t)level->width * 4];
    }
    MTLSamplerDescriptor *sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = description->min_filter == CC_INDEXED_LINEAR
                            ? MTLSamplerMinMagFilterLinear
                            : MTLSamplerMinMagFilterNearest;
    sampler.magFilter = description->mag_filter == CC_INDEXED_LINEAR
                            ? MTLSamplerMinMagFilterLinear
                            : MTLSamplerMinMagFilterNearest;
    static const MTLSamplerMipFilter mips[] = {MTLSamplerMipFilterNotMipmapped,
                                               MTLSamplerMipFilterNearest,
                                               MTLSamplerMipFilterLinear};
    static const MTLSamplerAddressMode wraps[] = {MTLSamplerAddressModeClampToEdge,
                                                  MTLSamplerAddressModeRepeat,
                                                  MTLSamplerAddressModeMirrorRepeat};
    sampler.mipFilter = mips[description->mip_filter];
    sampler.sAddressMode = wraps[description->wrap_s];
    sampler.tAddressMode = wraps[description->wrap_t];
    sampler.lodMinClamp = description->min_lod;
    sampler.lodMaxClamp = description->max_lod;
    sampler.maxAnisotropy = description->max_anisotropy;
    objects.sampler = [native->device newSamplerStateWithDescriptor:sampler];
    if (!objects.sampler) {
        fail(error, error_capacity, "cannot create indexed Metal sampler");
        return NULL;
    }
    CcIndexedTexture *texture = calloc(1, sizeof(*texture));
    if (!texture) {
        fail(error, error_capacity, "cannot allocate indexed texture");
        return NULL;
    }
    texture->owner = renderer;
    texture->objects = (__bridge_retained void *)objects;
    texture->next = renderer->textures;
    renderer->textures = texture;
    return texture;
}

static bool prepare_drawable(CcIndexedRenderer *renderer, char *error,
                             size_t error_capacity) {
    CcMetalState *native = native_state(renderer);
    if (!native->window.isVisible)
        return fail(error, error_capacity, "indexed native window is not visible");
    NSSize size = [native->view convertSizeToBacking:native->view.bounds.size];
    if (!isfinite(size.width) || !isfinite(size.height) || size.width < 1.0 ||
        size.height < 1.0 || size.width > INT_MAX || size.height > INT_MAX)
        return fail(error, error_capacity, "indexed drawable dimensions are invalid");
    NSUInteger width = (NSUInteger)llround(size.width);
    NSUInteger height = (NSUInteger)llround(size.height);
    native->layer.contentsScale = native->window.backingScaleFactor;
    native->layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    frames.drawable = [native->layer nextDrawable];
    if (!frames.drawable)
        return fail(error, error_capacity, "indexed Metal drawable unavailable");
    renderer->viewport = cc_viewport_fit((int)width, (int)height);
    return true;
}

static bool prepare_depth(CcIndexedRenderer *renderer, size_t slot, char *error,
                          size_t error_capacity) {
    if (!renderer->frame.depth_attachment)
        return true;
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    NSUInteger width = frames.drawable.texture.width;
    NSUInteger height = frames.drawable.texture.height;
    id<MTLTexture> depth = frames->depth[slot];
    if (depth && depth.width == width && depth.height == height)
        return true;
    MTLTextureDescriptor *layout = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:width
                                    height:height
                                 mipmapped:NO];
    layout.usage = MTLTextureUsageRenderTarget;
    layout.storageMode = MTLStorageModePrivate;
    CcMetalState *native = native_state(renderer);
    frames->depth[slot] = [native->device newTextureWithDescriptor:layout];
    if (!frames->depth[slot])
        return fail(error, error_capacity, "cannot create indexed depth attachment");
    return true;
}

static bool begin_frame(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                        char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_frame_validate(frame, error, error_capacity))
        return false;
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    size_t slot = renderer->frame_number % CC_IN_FLIGHT_FRAMES;
    [frames->commands[slot] waitUntilCompleted];
    if (frames->commands[slot].status == MTLCommandBufferStatusError) {
        metal_error(error, error_capacity, frames->commands[slot].error,
                    "previous indexed Metal frame failed");
        frames->commands[slot] = nil;
        return false;
    }
    frames->commands[slot] = nil;
    for (CcIndexedMesh *mesh = renderer->meshes; mesh; mesh = mesh->next) {
        if (mesh->dirty[slot]) {
            CcIndexedMetalMesh *objects = (__bridge CcIndexedMetalMesh *)mesh->objects;
            memcpy(objects->frameVertices[slot].contents, mesh->staging,
                   mesh->vertex_bytes);
            mesh->dirty[slot] = false;
        }
    }
    renderer->frame = *frame;
    if (!prepare_drawable(renderer, error, error_capacity) ||
        !prepare_depth(renderer, slot, error, error_capacity)) {
        frames.drawable = nil;
        return false;
    }
    renderer->draw_count = 0;
    renderer->active = true;
    return true;
}

static bool draw_validate(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                          char *error, size_t error_capacity) {
    if (!renderer || !renderer->active || !draw || !draw->program || !draw->mesh ||
        renderer->draw_count >= renderer->capacity ||
        draw->program->owner != renderer || draw->mesh->owner != renderer ||
        draw->mesh->vertex_stride != draw->program->vertex_stride ||
        draw->first_index > draw->mesh->index_count ||
        draw->index_count > draw->mesh->index_count - draw->first_index ||
        draw->first_index % 3 || draw->index_count % 3 ||
        draw->texture_count != draw->program->texture_count)
        return fail(error, error_capacity, "invalid indexed draw ownership or range");
    if (!cc_indexed_uniforms_validate(draw->vertex_uniforms, draw->vertex_uniform_count,
                                      draw->program->vertex_uniform_count, error,
                                      error_capacity) ||
        !cc_indexed_uniforms_validate(
            draw->fragment_uniforms, draw->fragment_uniform_count,
            draw->program->fragment_uniform_count, error, error_capacity))
        return false;
    for (size_t index = 0; index < draw->texture_count; ++index) {
        if (!draw->textures[index] || draw->textures[index]->owner != renderer)
            return fail(error, error_capacity, "invalid indexed texture ownership");
    }
    return true;
}

bool cc_indexed_draw(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                     char *error, size_t error_capacity) {
    if (!draw_validate(renderer, draw, error, error_capacity))
        return false;
    size_t index = renderer->draw_count;
    CcIndexedMetalDraw *command = &renderer->draws[index];
    *command = (CcIndexedMetalDraw){.program = draw->program,
                                    .mesh = draw->mesh,
                                    .texture_count = draw->texture_count,
                                    .first_index = draw->first_index,
                                    .index_count = draw->index_count};
    memcpy(command->textures, draw->textures, sizeof(command->textures));
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    size_t slot = renderer->frame_number % CC_IN_FLIGHT_FRAMES;
    uint8_t *uniforms = frames->uniforms[slot].contents;
    size_t offset = index * CC_INDEXED_UNIFORM_BYTES * 2;
    if (draw->vertex_uniform_count)
        memcpy(uniforms + offset, draw->vertex_uniforms,
               draw->vertex_uniform_count * sizeof(float) * 4);
    if (draw->fragment_uniform_count)
        memcpy(uniforms + offset + CC_INDEXED_UNIFORM_BYTES, draw->fragment_uniforms,
               draw->fragment_uniform_count * sizeof(float) * 4);
    ++renderer->draw_count;
    return true;
}

static void encode_draw(CcIndexedRenderer *renderer,
                        id<MTLRenderCommandEncoder> encoder, size_t index,
                        id<MTLBuffer> uniforms) {
    const CcIndexedMetalDraw *draw = &renderer->draws[index];
    const CcIndexedProgram *program = draw->program;
    CcIndexedMetalProgram *objects = (__bridge CcIndexedMetalProgram *)program->objects;
    CcIndexedMetalMesh *mesh = (__bridge CcIndexedMetalMesh *)draw->mesh->objects;
    bool depth = renderer->frame.depth_attachment;
    [encoder
        setRenderPipelineState:depth ? objects.depthPipeline : objects.colorPipeline];
    [encoder
        setDepthStencilState:depth ? objects.depthState : objects.disabledDepthState];
    [encoder setBlendColorRed:program->state.blend_color[0]
                        green:program->state.blend_color[1]
                         blue:program->state.blend_color[2]
                        alpha:program->state.blend_color[3]];
    static const MTLCullMode culls[] = {MTLCullModeNone, MTLCullModeFront,
                                        MTLCullModeBack};
    [encoder setCullMode:culls[program->state.cull]];
    [encoder setFrontFacingWinding:program->state.counterclockwise_front
                                       ? MTLWindingCounterClockwise
                                       : MTLWindingClockwise];
    size_t slot = renderer->frame_number % CC_IN_FLIGHT_FRAMES;
    id<MTLBuffer> vertices =
        draw->mesh->staging ? mesh->frameVertices[slot] : mesh.vertices;
    [encoder setVertexBuffer:vertices offset:0 atIndex:0];
    size_t offset = index * CC_INDEXED_UNIFORM_BYTES * 2;
    if (program->vertex_uniform_count)
        [encoder setVertexBuffer:uniforms
                          offset:offset
                         atIndex:program->vertex_uniform_buffer];
    if (program->fragment_uniform_count)
        [encoder setFragmentBuffer:uniforms
                            offset:offset + CC_INDEXED_UNIFORM_BYTES
                           atIndex:program->fragment_uniform_buffer];
    for (size_t unit = 0; unit < draw->texture_count; ++unit) {
        CcIndexedMetalTexture *texture =
            (__bridge CcIndexedMetalTexture *)draw->textures[unit]->objects;
        [encoder setFragmentTexture:texture.texture atIndex:unit];
        [encoder setFragmentSamplerState:texture.sampler atIndex:unit];
    }
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:draw->index_count
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:mesh.indices
                 indexBufferOffset:draw->first_index * sizeof(uint16_t)];
}

static bool end_frame(CcIndexedRenderer *renderer, char *error, size_t error_capacity) {
    if (!renderer || !renderer->active)
        return fail(error, error_capacity, "indexed frame is not active");
    renderer->active = false;
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    size_t slot = renderer->frame_number % CC_IN_FLIGHT_FRAMES;
    CcMetalState *native = native_state(renderer);
    id<MTLCommandBuffer> commands = [native->command_queue commandBuffer];
    MTLRenderPassColorAttachmentDescriptor *color = frames.pass.colorAttachments[0];
    color.texture = frames.drawable.texture;
    color.loadAction =
        renderer->frame.clear_color_enabled ? MTLLoadActionClear : MTLLoadActionLoad;
    color.storeAction = MTLStoreActionStore;
    CcColor clear = renderer->frame.clear_color;
    color.clearColor = MTLClearColorMake(clear.r, clear.g, clear.b, clear.a);
    frames.pass.depthAttachment.texture =
        renderer->frame.depth_attachment ? frames->depth[slot] : nil;
    frames.pass.depthAttachment.loadAction =
        renderer->frame.clear_depth_enabled ? MTLLoadActionClear : MTLLoadActionLoad;
    frames.pass.depthAttachment.storeAction = MTLStoreActionStore;
    frames.pass.depthAttachment.clearDepth = renderer->frame.clear_depth;
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:frames.pass];
    color.texture = nil;
    frames.pass.depthAttachment.texture = nil;
    if (!commands || !encoder) {
        frames.drawable = nil;
        return fail(error, error_capacity, "cannot encode indexed Metal frame");
    }
    CcViewport viewport = renderer->viewport;
    [encoder setViewport:(MTLViewport){viewport.x, viewport.y, viewport.width,
                                       viewport.height, 0.0, 1.0}];
    MTLScissorRect scissor = {(NSUInteger)viewport.x, (NSUInteger)viewport.y,
                              (NSUInteger)viewport.width, (NSUInteger)viewport.height};
    [encoder setScissorRect:scissor];
    for (size_t index = 0; index < renderer->draw_count; ++index)
        encode_draw(renderer, encoder, index, frames->uniforms[slot]);
    [encoder endEncoding];
    [commands presentDrawable:frames.drawable];
    [commands commit];
    frames->commands[slot] = commands;
    frames.drawable = nil;
    ++renderer->frame_number;
    return true;
}

static bool wait_frames(CcIndexedRenderer *renderer, char *error,
                        size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    CcIndexedMetalFrames *frames = (__bridge CcIndexedMetalFrames *)renderer->objects;
    bool valid = true;
    for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index) {
        [frames->commands[index] waitUntilCompleted];
        if (frames->commands[index].status == MTLCommandBufferStatusError) {
            metal_error(error, error_capacity, frames->commands[index].error,
                        "indexed Metal execution failed");
            valid = false;
        }
        frames->commands[index] = nil;
    }
    return valid;
}

CcIndexedRenderer *cc_indexed_create(CcPlatform *platform, char *error,
                                     size_t error_capacity) {
    @autoreleasepool {
        return create_renderer(platform, error, error_capacity);
    }
}

bool cc_indexed_reserve(CcIndexedRenderer *renderer, size_t draw_count, char *error,
                        size_t error_capacity) {
    @autoreleasepool {
        return reserve_draws(renderer, draw_count, error, error_capacity);
    }
}

CcIndexedProgram *
cc_indexed_program_create(CcIndexedRenderer *renderer,
                          const CcIndexedProgramDescription *description, char *error,
                          size_t error_capacity) {
    @autoreleasepool {
        return create_program(renderer, description, error, error_capacity);
    }
}

CcIndexedMesh *cc_indexed_mesh_create(CcIndexedRenderer *renderer,
                                      const CcIndexedMeshDescription *description,
                                      char *error, size_t error_capacity) {
    @autoreleasepool {
        return create_mesh(renderer, description, error, error_capacity, false);
    }
}

CcIndexedMesh *
cc_indexed_mesh_create_dynamic(CcIndexedRenderer *renderer,
                               const CcIndexedMeshDescription *description, char *error,
                               size_t error_capacity) {
    @autoreleasepool {
        return create_mesh(renderer, description, error, error_capacity, true);
    }
}

bool cc_indexed_mesh_update(CcIndexedRenderer *renderer, CcIndexedMesh *mesh,
                            const void *vertices, size_t vertex_bytes, char *error,
                            size_t error_capacity) {
    @autoreleasepool {
        if (!prepare_resources(renderer, error, error_capacity))
            return false;
        CcIndexedMesh *member = renderer->meshes;
        while (member && member != mesh)
            member = member->next;
        if (!member || !member->staging || !vertices ||
            vertex_bytes != member->vertex_bytes)
            return fail(error, error_capacity, "invalid indexed mesh update");
        memcpy(member->staging, vertices, vertex_bytes);
        for (size_t index = 0; index < CC_IN_FLIGHT_FRAMES; ++index)
            member->dirty[index] = true;
        return true;
    }
}

bool cc_indexed_program_release(CcIndexedRenderer *renderer, CcIndexedProgram **program,
                                char *error, size_t error_capacity) {
    @autoreleasepool {
        if (!program)
            return fail(error, error_capacity, "missing indexed program handle");
        if (!prepare_resources(renderer, error, error_capacity))
            return false;
        if (!*program)
            return true;
        CcIndexedProgram **slot = &renderer->programs;
        while (*slot && *slot != *program)
            slot = &(*slot)->next;
        if (!*slot)
            return fail(error, error_capacity,
                        "indexed program belongs to another renderer");
        CcIndexedProgram *released = *slot;
        *slot = released->next;
        (void)CFBridgingRelease(released->objects);
        free(released);
        *program = NULL;
        return true;
    }
}

bool cc_indexed_mesh_release(CcIndexedRenderer *renderer, CcIndexedMesh **mesh,
                             char *error, size_t error_capacity) {
    @autoreleasepool {
        if (!mesh)
            return fail(error, error_capacity, "missing indexed mesh handle");
        if (!prepare_resources(renderer, error, error_capacity))
            return false;
        if (!*mesh)
            return true;
        CcIndexedMesh **slot = &renderer->meshes;
        while (*slot && *slot != *mesh)
            slot = &(*slot)->next;
        if (!*slot)
            return fail(error, error_capacity,
                        "indexed mesh belongs to another renderer");
        CcIndexedMesh *released = *slot;
        *slot = released->next;
        (void)CFBridgingRelease(released->objects);
        free(released->staging);
        free(released);
        *mesh = NULL;
        return true;
    }
}

bool cc_indexed_texture_release(CcIndexedRenderer *renderer, CcIndexedTexture **texture,
                                char *error, size_t error_capacity) {
    @autoreleasepool {
        if (!texture)
            return fail(error, error_capacity, "missing indexed texture handle");
        if (!prepare_resources(renderer, error, error_capacity))
            return false;
        if (!*texture)
            return true;
        CcIndexedTexture **slot = &renderer->textures;
        while (*slot && *slot != *texture)
            slot = &(*slot)->next;
        if (!*slot)
            return fail(error, error_capacity,
                        "indexed texture belongs to another renderer");
        CcIndexedTexture *released = *slot;
        *slot = released->next;
        (void)CFBridgingRelease(released->objects);
        free(released);
        *texture = NULL;
        return true;
    }
}

CcIndexedTexture *
cc_indexed_texture_create(CcIndexedRenderer *renderer,
                          const CcIndexedTextureDescription *description, char *error,
                          size_t error_capacity) {
    @autoreleasepool {
        return create_texture(renderer, description, error, error_capacity);
    }
}

bool cc_indexed_begin(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                      char *error, size_t error_capacity) {
    @autoreleasepool {
        return begin_frame(renderer, frame, error, error_capacity);
    }
}

bool cc_indexed_end(CcIndexedRenderer *renderer, char *error, size_t error_capacity) {
    @autoreleasepool {
        return end_frame(renderer, error, error_capacity);
    }
}

bool cc_indexed_wait(CcIndexedRenderer *renderer, char *error, size_t error_capacity) {
    @autoreleasepool {
        return wait_frames(renderer, error, error_capacity);
    }
}
