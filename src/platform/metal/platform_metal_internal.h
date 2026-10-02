#ifndef CC_PLATFORM_METAL_INTERNAL_H
#define CC_PLATFORM_METAL_INTERNAL_H

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "console_common/platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

#if !__has_feature(objc_arc)
#error "The Metal platform adapter must be compiled with -fobjc-arc."
#endif

#define CC_IN_FLIGHT_FRAMES 3

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
    uint32_t stage_words[6][4];
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
    CcMetalView *view;
    CcWindowDelegate *window_delegate;
    CAMetalLayer *layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLFunction> vertex_function;
    id<MTLFunction> material_fragment_function;
    id<MTLFunction> tev_fragment_function;
    NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *material_pipelines;
    id<MTLDepthStencilState> depth_states[17];
    id<MTLTexture> window_depth[CC_IN_FLIGHT_FRAMES];
    id<MTLTexture> target_depth[CC_IN_FLIGHT_FRAMES];
    id<MTLSamplerState> samplers[3][3];
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
}
@end

/* GPU initialization precedes window creation. The window adapter retains
 * CcMetalState through platform->metal_state until cc_platform_destroy. */
bool cc_prepare_metal(CcMetalState *state);
void cc_wait_for_metal(CcMetalState *state);
void cc_release_metal(CcMetalState *state);

#endif
