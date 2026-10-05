#ifndef CONSOLE_COMMON_PLATFORM_INDEXED_H
#define CONSOLE_COMMON_PLATFORM_INDEXED_H

#include "console_common/platform/platform.h"
#include "console_common/render/viewport.h"

#include <stddef.h>

enum {
    CC_INDEXED_ATTRIBUTES = 8,
    CC_INDEXED_UNIFORMS = 64,
    CC_INDEXED_TEXTURES = 8,
    CC_INDEXED_MIP_LEVELS = 16
};

typedef struct CcIndexedRenderer CcIndexedRenderer;
typedef struct CcIndexedProgram CcIndexedProgram;
typedef struct CcIndexedMesh CcIndexedMesh;
typedef struct CcIndexedTexture CcIndexedTexture;
typedef struct CcIndexedTarget CcIndexedTarget;

typedef enum CcIndexedCompare {
    CC_INDEXED_NEVER,
    CC_INDEXED_LESS,
    CC_INDEXED_EQUAL,
    CC_INDEXED_LESS_EQUAL,
    CC_INDEXED_GREATER,
    CC_INDEXED_NOT_EQUAL,
    CC_INDEXED_GREATER_EQUAL,
    CC_INDEXED_ALWAYS
} CcIndexedCompare;

typedef enum CcIndexedBlendFactor {
    CC_INDEXED_ZERO,
    CC_INDEXED_ONE,
    CC_INDEXED_SOURCE_COLOR,
    CC_INDEXED_INVERSE_SOURCE_COLOR,
    CC_INDEXED_DESTINATION_COLOR,
    CC_INDEXED_INVERSE_DESTINATION_COLOR,
    CC_INDEXED_SOURCE_ALPHA,
    CC_INDEXED_INVERSE_SOURCE_ALPHA,
    CC_INDEXED_DESTINATION_ALPHA,
    CC_INDEXED_INVERSE_DESTINATION_ALPHA,
    CC_INDEXED_CONSTANT_COLOR
} CcIndexedBlendFactor;

typedef enum CcIndexedBlendEquation {
    CC_INDEXED_ADD,
    CC_INDEXED_SUBTRACT,
    CC_INDEXED_REVERSE_SUBTRACT
} CcIndexedBlendEquation;

typedef enum CcIndexedCull {
    CC_INDEXED_CULL_NONE,
    CC_INDEXED_CULL_FRONT,
    CC_INDEXED_CULL_BACK
} CcIndexedCull;

typedef struct CcIndexedState {
    bool blend;
    CcIndexedBlendEquation equation_rgb;
    CcIndexedBlendEquation equation_alpha;
    CcIndexedBlendFactor source_rgb;
    CcIndexedBlendFactor destination_rgb;
    CcIndexedBlendFactor source_alpha;
    CcIndexedBlendFactor destination_alpha;
    bool depth_test;
    bool depth_write;
    CcIndexedCompare depth_compare;
    CcIndexedCull cull;
    bool counterclockwise_front;
    bool color_write[4];
    /* Retained normalized RGBA for CONSTANT_COLOR. Backends publish this
     * explicit value at drawing; program creation never infers it. */
    float blend_color[4];
} CcIndexedState;

typedef struct CcIndexedAttribute {
    const char *name;
    size_t offset;
} CcIndexedAttribute;

typedef struct CcIndexedProgramDescription {
    const char *gles_vertex_source;
    const char *gles_fragment_source;
    const char *metal_vertex_source;
    const char *metal_fragment_source;
    const char *metal_vertex_entry;
    const char *metal_fragment_entry;
    CcIndexedAttribute attributes[CC_INDEXED_ATTRIBUTES];
    size_t attribute_count;
    size_t vertex_stride;
    const char *vertex_uniforms[CC_INDEXED_UNIFORMS];
    size_t vertex_uniform_count;
    const char *fragment_uniforms[CC_INDEXED_UNIFORMS];
    size_t fragment_uniform_count;
    const char *texture_uniforms[CC_INDEXED_TEXTURES];
    size_t texture_count;
    unsigned metal_vertex_uniform_buffer;
    unsigned metal_fragment_uniform_buffer;
    CcIndexedState state;
} CcIndexedProgramDescription;

typedef struct CcIndexedMeshDescription {
    const void *vertices;
    size_t vertex_count;
    size_t vertex_stride;
    const uint16_t *indices;
    size_t index_count;
} CcIndexedMeshDescription;

typedef enum CcIndexedFilter { CC_INDEXED_NEAREST, CC_INDEXED_LINEAR } CcIndexedFilter;

typedef enum CcIndexedMipFilter {
    CC_INDEXED_MIP_NONE,
    CC_INDEXED_MIP_NEAREST,
    CC_INDEXED_MIP_LINEAR
} CcIndexedMipFilter;

typedef enum CcIndexedWrap {
    CC_INDEXED_CLAMP,
    CC_INDEXED_REPEAT,
    CC_INDEXED_MIRROR
} CcIndexedWrap;

typedef struct CcIndexedMip {
    const uint8_t *rgba;
    size_t size;
    unsigned width;
    unsigned height;
} CcIndexedMip;

typedef struct CcIndexedTextureDescription {
    CcIndexedMip levels[CC_INDEXED_MIP_LEVELS];
    size_t level_count;
    CcIndexedFilter min_filter;
    CcIndexedFilter mag_filter;
    CcIndexedMipFilter mip_filter;
    CcIndexedWrap wrap_s;
    CcIndexedWrap wrap_t;
    float min_lod;
    float max_lod;
    unsigned max_anisotropy;
} CcIndexedTextureDescription;

typedef struct CcIndexedFrame {
    CcColor clear_color;
    float clear_depth;
    bool clear_color_enabled;
    bool clear_depth_enabled;
    bool depth_attachment;
} CcIndexedFrame;

typedef enum CcIndexedColorFormat {
    CC_INDEXED_RGBA8,
    CC_INDEXED_RGBA16_FLOAT
} CcIndexedColorFormat;

typedef struct CcIndexedTargetDescription {
    unsigned width;
    unsigned height;
    CcIndexedColorFormat color_format;
    bool depth_attachment;
    CcIndexedFilter min_filter;
    CcIndexedFilter mag_filter;
} CcIndexedTargetDescription;

typedef enum CcIndexedLoad {
    CC_INDEXED_LOAD,
    CC_INDEXED_CLEAR,
    CC_INDEXED_DISCARD
} CcIndexedLoad;

typedef struct CcIndexedPass {
    /* NULL selects the frame's drawable, otherwise a renderer-owned target. */
    CcIndexedTarget *target;
    CcViewport viewport;
    CcIndexedLoad color_load;
    CcIndexedLoad depth_load;
    CcColor clear_color;
    float clear_depth;
    bool depth_attachment;
    /* With DISCARD, the caller guarantees complete attachment coverage before
     * another pass loads or samples it. The backend does not infer coverage. */
    bool color_full_write;
    bool depth_full_write;
    /* False retains a bounded viewport and uses that same rectangle as scissor.
     * True supplies independent top-left rectangles: viewport origins may be
     * negative and either rectangle may extend beyond the attachment. Scissor
     * origins and sizes are nonnegative. Backend attachment clipping leaves
     * these requested values unchanged; an empty intersection draws nothing. */
    bool scissor_enabled;
    CcViewport scissor;
} CcIndexedPass;

typedef struct CcIndexedDraw {
    CcIndexedProgram *program;
    CcIndexedMesh *mesh;
    const float (*vertex_uniforms)[4];
    size_t vertex_uniform_count;
    const float (*fragment_uniforms)[4];
    size_t fragment_uniform_count;
    CcIndexedTexture *textures[CC_INDEXED_TEXTURES];
    size_t texture_count;
    size_t first_index;
    size_t index_count;
} CcIndexedDraw;

/* The platform owns the native window and must outlive its indexed renderer.
 * Indexed frames exclusively use that window; do not mix quad/TEV submissions
 * into them. Destroy the renderer before destroying the platform. All program,
 * mesh and texture handles belong to one renderer. Individual release clears
 * the caller's handle; destroying the renderer releases any remaining handles.
 * Creation copies/uploads borrowed descriptions. Draw copies uniform rows into
 * backend-owned storage immediately. Resource creation and frame preparation
 * happen on the host render thread, outside an active frame.
 *
 * Each attribute is a float4 at its compact array index, with caller-specified
 * byte offset and stride. Indices are uint16 triangles. Uniform names identify
 * GLES vec4 values; Metal receives the same contiguous rows at the configured
 * stage buffer indices. Vertex buffer zero is reserved for mesh attributes.
 * Alpha tests and clip-space conventions belong to supplied shader sources.
 * ES2 NPOT textures require clamp sampling without mip filtering; unsupported
 * combinations fail explicitly. LOD bounds are finite, nonnegative and ordered;
 * anisotropy is explicitly 1 through 16. Metal preserves all three requests.
 * Core ES2 supports anisotropy 1, minimum LOD 0, and a maximum LOD that does not
 * exclude any sampled level. Unsupported requests fail before texture upload.
 * Clearing covers the whole drawable; the fitted
 * viewport uses the shared display aspect. Depth requests require the frame's
 * depth attachment; disabled depth testing suppresses depth writes. Uniform
 * names in the two stages must be distinct, since GLES shares one namespace.
 * No per-frame shader compilation,
 * texture upload or application heap allocation occurs after reserve succeeds;
 * native command submission and drawable resize retain their host lifecycle. */
CcIndexedRenderer *cc_indexed_create(CcPlatform *platform, char *error,
                                     size_t error_capacity);
void cc_indexed_destroy(CcIndexedRenderer *renderer);
bool cc_indexed_reserve(CcIndexedRenderer *renderer, size_t draw_count, char *error,
                        size_t error_capacity);
/* Pass storage and targets are prepared outside frames. Targets own one color
 * texture and optional depth storage at fixed dimensions. Sampling is clamp,
 * without mip levels, using the explicitly requested min/mag filters. Metal
 * supports RGBA8 and RGBA16Float; core GLES2 supports RGBA8 and rejects float
 * targets explicitly. It never substitutes an eight-bit target for HDR.
 * Borrowed target textures cannot be individually released and expire when
 * their target is released. Supplied backend shaders own texture-coordinate
 * and clip-space conventions, as for ordinary indexed textures. */
bool cc_indexed_reserve_passes(CcIndexedRenderer *renderer, size_t pass_count,
                               char *error, size_t error_capacity);
CcIndexedTarget *cc_indexed_target_create(CcIndexedRenderer *renderer,
                                          const CcIndexedTargetDescription *description,
                                          char *error, size_t error_capacity);
CcIndexedTexture *cc_indexed_target_texture(CcIndexedRenderer *renderer,
                                            CcIndexedTarget *target);
bool cc_indexed_target_release(CcIndexedRenderer *renderer, CcIndexedTarget **target,
                               char *error, size_t error_capacity);
/* Prepare drawable depth outside frames, including after a drawable resize.
 * Metal reuses matching attachments; GLES2 verifies actual host depth storage.
 * The empty-frame entry never allocates a missing drawable depth attachment. */
bool cc_indexed_prepare_drawable_depth(CcIndexedRenderer *renderer, char *error,
                                       size_t error_capacity);
CcIndexedProgram *
cc_indexed_program_create(CcIndexedRenderer *renderer,
                          const CcIndexedProgramDescription *description, char *error,
                          size_t error_capacity);
CcIndexedMesh *cc_indexed_mesh_create(CcIndexedRenderer *renderer,
                                      const CcIndexedMeshDescription *description,
                                      char *error, size_t error_capacity);
/* Fixed-capacity dynamic meshes preserve the immutable index list and vertex
 * layout. Update replaces exactly the original vertex byte span without
 * application heap allocation or GPU buffer recreation. Call on the owning
 * render thread outside a frame, or before the first accepted draw using that
 * mesh in an active frame. Accepted deferred Metal draws also mark use; later
 * updates reject before mutation, including after an accepted empty draw.
 * Metal changes only a retired in-flight slot and stages other slots. GLES2
 * uses core BufferSubData and driver synchronization may block. An entered
 * GLES2 update failure latches an active frame failure and invalidates vertices
 * until a successful complete update repairs them. Cold validation failures
 * preserve storage and allow retry. Ordinary immutable meshes reject updates.
 * Source data is borrowed only for the call and must be bounded and disjoint
 * from renderer-owned controls/storage and the writable diagnostic view. */
CcIndexedMesh *
cc_indexed_mesh_create_dynamic(CcIndexedRenderer *renderer,
                               const CcIndexedMeshDescription *description, char *error,
                               size_t error_capacity);
bool cc_indexed_mesh_update(CcIndexedRenderer *renderer, CcIndexedMesh *mesh,
                            const void *vertices, size_t vertex_bytes, char *error,
                            size_t error_capacity);
CcIndexedTexture *
cc_indexed_texture_create(CcIndexedRenderer *renderer,
                          const CcIndexedTextureDescription *description, char *error,
                          size_t error_capacity);
/* Opt-in fixed-layout RGBA8 textures copy their initial mip data and sampler.
 * Update replaces one complete original mip without allocating application
 * storage or recreating GPU textures. Geometry, level count and sampler stay
 * fixed; ordinary textures and render-target textures reject updates.
 *
 * Call on the owning render thread, either outside a frame or inside it before
 * the first accepted draw using this texture. Acceptance marks use even when
 * Metal defers encoding. Later updates in that frame reject before mutation.
 * Metal owns one texture per in-flight slot and changes a slot only after its
 * prior command retires; GLES2 uses core TexSubImage2D and may synchronize in
 * the driver. An entered GLES2 update failure latches an active frame failure
 * and invalidates that mip until a successful full update repairs it. Cold
 * validation failures preserve the texture and allow retry.
 * Source data is borrowed only for the call and must be bounded and disjoint
 * from renderer-owned controls/storage and the writable diagnostic view. */
CcIndexedTexture *
cc_indexed_texture_create_dynamic(CcIndexedRenderer *renderer,
                                  const CcIndexedTextureDescription *description,
                                  char *error, size_t error_capacity);
bool cc_indexed_texture_update(CcIndexedRenderer *renderer, CcIndexedTexture *texture,
                               size_t level, const uint8_t *rgba, size_t byte_count,
                               char *error, size_t error_capacity);
/* Release on the owning render thread outside an active frame. Previously
 * submitted native commands retain their GPU resources until completion; no
 * explicit wait occurs here. Empty handles succeed. Wrong-owner/nonmember
 * handles fail without dereferencing them or changing the caller's handle.
 * Other aliases become invalid after success and must not be used again. */
bool cc_indexed_program_release(CcIndexedRenderer *renderer, CcIndexedProgram **program,
                                char *error, size_t error_capacity);
bool cc_indexed_mesh_release(CcIndexedRenderer *renderer, CcIndexedMesh **mesh,
                             char *error, size_t error_capacity);
bool cc_indexed_texture_release(CcIndexedRenderer *renderer, CcIndexedTexture **texture,
                                char *error, size_t error_capacity);
bool cc_indexed_begin(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                      char *error, size_t error_capacity);
/* Starts an empty ordered frame; the frame specifies drawable depth availability.
 * Its clear values are used only by the legacy begin wrapper. Each pass explicitly
 * selects its load/clear policy and top-left viewport/scissor. Clears cover the
 * entire attachment. A following pass completes/stores the previous pass. Fresh
 * target LOAD/sample and current-target feedback fail before submission. End
 * requires a drawable pass. Ordinary end performs no diagnostic wait/readback;
 * opt-in platform capture retains its existing synchronization contract.
 * GLES2 checks GL_MAX_VIEWPORT_DIMS and exact signed bottom-origin conversion
 * before entering a pass, then submits the requested scissor unchanged. Metal
 * sends the requested viewport and intersects scissor with the attachment for
 * its unsigned scissor API; an empty intersection skips draw encoding. Neither
 * backend changes the stored scene request or adds a clear for partial drawing. */
/* Metal's empty-frame entry rejects a still-busy in-flight storage slot instead
 * of waiting. The legacy begin wrapper keeps its existing slot-reuse wait. */
bool cc_indexed_begin_passes(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                             char *error, size_t error_capacity);
bool cc_indexed_pass_begin(CcIndexedRenderer *renderer, const CcIndexedPass *pass,
                           char *error, size_t error_capacity);
bool cc_indexed_draw(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                     char *error, size_t error_capacity);
bool cc_indexed_end(CcIndexedRenderer *renderer, char *error, size_t error_capacity);
/* Explicit synchronization for diagnostics or shutdown, outside active frames.
 * Ordinary frame submission does not call this blocking operation. */
bool cc_indexed_wait(CcIndexedRenderer *renderer, char *error, size_t error_capacity);

#endif
