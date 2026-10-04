#ifndef CC_PLATFORM_METAL_INTERNAL_H
#define CC_PLATFORM_METAL_INTERNAL_H

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "console_common/platform/platform.h"
#include "material.h"

#include <stdbool.h>
#include <stdint.h>

#if !__has_feature(objc_arc)
#error "The Metal platform adapter must be compiled with -fobjc-arc."
#endif

#define CC_IN_FLIGHT_FRAMES 3

enum {
    CC_BLEND_DISABLED = 64,
    CC_BLEND_DEFAULT = 4 * 8 + 5,
    CC_MATERIAL_PIPELINE_VARIANTS = CC_BLEND_DISABLED + 1
};

typedef struct CcVertex {
    float x;
    float y;
    float uv[CC_MATERIAL_TEXTURES][2];
    float depth;
    float clip_w;
    CcColor color;
} CcVertex;

_Static_assert(sizeof(CcVertex) == 16 * sizeof(float),
               "The C and Metal vertex layouts must match.");

typedef struct CcMaterialParams {
    float r0[4];
    float r1[4];
    float kc3[4];
    uint32_t texture_count;
    uint32_t alpha_comparisons;
    uint32_t alpha_operation;
    uint32_t alpha_references;
} CcMaterialParams;

_Static_assert(sizeof(CcMaterialParams) == 16 * sizeof(float),
               "The C and Metal material layouts must match.");

/* Four stage bytes per word keep the fragment parameter block compact while
 * preserving the raw BRLYT encoding. */
typedef struct CcTevParams {
    float registers[3][4];
    float konst_colors[4][4];
    uint32_t stage_words[CC_RENDER_TEV_STAGES][4];
    uint32_t swap[4];
    uint32_t stage_count;
    uint32_t alpha_comparisons;
    uint32_t alpha_operation;
    uint32_t alpha_references;
} CcTevParams;

_Static_assert(sizeof(CcTevParams) == 60 * sizeof(float),
               "The C and Metal TEV layouts must match.");

typedef enum CcBatchKind {
    CC_BATCH_BASIC,
    CC_BATCH_MATERIAL,
    CC_BATCH_TEV
} CcBatchKind;

typedef struct CcBatchState {
    CcBatchKind kind;
    bool clip_enabled;
    CcClipRect clip;
    uint32_t textures[CC_MATERIAL_TEXTURES];
    uint8_t wrap_s[CC_MATERIAL_TEXTURES];
    uint8_t wrap_t[CC_MATERIAL_TEXTURES];
    bool nearest[CC_MATERIAL_TEXTURES];
    uint8_t blend_key;
    uint8_t depth_key;
    union {
        CcMaterialParams simple;
        CcTevParams tev;
    } params;
} CcBatchState;

typedef struct CcBatch {
    CcBatchState state;
    size_t first_vertex;
    size_t vertex_count;
} CcBatch;

struct CcPlatform {
    void *metal_state;
    CcEvent *events;
    size_t event_count;
    size_t event_read;
    size_t event_capacity;
    float fade_alpha;
};

@interface CcMetalView : NSView
@property(nonatomic, assign) CcPlatform *platform;
@property(nonatomic, strong) NSCursor *hiddenCursor;
@property(nonatomic, strong) NSTrackingArea *pointerTrackingArea;
@end

@interface CcWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) CcPlatform *platform;
@property(nonatomic, weak) CcMetalView *view;
@end

@interface CcMetalState : NSObject {
  @public
    NSWindow *window;
    bool fullscreen_transitioning;
    CcMetalView *view;
    CcWindowDelegate *window_delegate;
    CAMetalLayer *layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLRenderPipelineState> presentation_pipeline;
    id<MTLRenderPipelineState> antialias_pipeline;
    id<MTLFunction> basic_fragment_function;
    NSUInteger antialias_samples;
    bool antialiasing;
    bool rendering_multisample;
    bool warned_antialiasing;
    id<MTLTexture> antialias_color[CC_IN_FLIGHT_FRAMES];
    id<MTLTexture> antialias_depth[CC_IN_FLIGHT_FRAMES];
    NSUInteger antialias_failed_width[CC_IN_FLIGHT_FRAMES];
    NSUInteger antialias_failed_height[CC_IN_FLIGHT_FRAMES];
    id<MTLRenderPipelineState>
        antialias_material_pipelines[2][CC_MATERIAL_PIPELINE_VARIANTS];
    bool antialias_pipeline_attempted[2][CC_MATERIAL_PIPELINE_VARIANTS];
    id<MTLFunction> vertex_function;
    id<MTLFunction> material_fragment_function;
    id<MTLFunction> tev_fragment_function;
    id<MTLRenderPipelineState> material_pipelines[2][CC_MATERIAL_PIPELINE_VARIANTS];
    bool material_pipeline_attempted[2][CC_MATERIAL_PIPELINE_VARIANTS];
    id<MTLDepthStencilState> depth_states[17];
    id<MTLTexture> window_depth[CC_IN_FLIGHT_FRAMES];
    id<MTLTexture> target_depth[CC_IN_FLIGHT_FRAMES];
    id<MTLSamplerState> samplers[2][3][3];
    MTLRenderPassDescriptor *render_pass;
    NSMutableArray *textures;
    NSMutableArray<NSNumber *> *free_texture_handles;
    NSMutableIndexSet *retired_texture_handles;
    bool warned_tev_encoding;
    id<MTLBuffer> vertex_buffers[CC_IN_FLIGHT_FRAMES];
    id<MTLCommandBuffer> in_flight[CC_IN_FLIGHT_FRAMES];
    NSUInteger buffer_sizes[CC_IN_FLIGHT_FRAMES];
    CcVertex *vertices;
    size_t vertex_count;
    size_t vertex_capacity;
    CcBatch *batches;
    size_t batch_count;
    size_t batch_capacity;
    CcColor clear_color;
    uint32_t render_target_handle;
    bool clip_enabled;
    CcClipRect clip;
    uint64_t frame_number;
    bool warned_tev_limit;
    id<MTLTexture> capture_texture;
    id<MTLBuffer> capture_readback;
    uint8_t *capture_rgba;
    size_t capture_stride;
    size_t capture_byte_count;
    int capture_width;
    int capture_height;
    bool capture_ready;
}
@end

/* GPU initialization precedes window creation. The window adapter retains
 * CcMetalState through platform->metal_state until cc_platform_destroy. */
bool cc_metal_prepare_pipelines(CcMetalState *state, id<MTLFunction> basic_fragment);
bool cc_metal_prepare_antialias_pipelines(CcMetalState *state);
id<MTLRenderPipelineState>
cc_metal_material_pipeline(CcMetalState *state, CcBatchKind kind, uint8_t blend_key);

bool cc_prepare_metal(CcMetalState *state);
/* Borrow the existing native device/window boundary for generic indexed draws. */
CcMetalState *cc_metal_platform_state(CcPlatform *platform);
void cc_wait_for_metal(CcMetalState *state);
void cc_release_metal(CcMetalState *state);

#endif
