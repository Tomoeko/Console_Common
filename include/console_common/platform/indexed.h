#ifndef CONSOLE_COMMON_PLATFORM_INDEXED_H
#define CONSOLE_COMMON_PLATFORM_INDEXED_H

#include "console_common/platform/platform.h"

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
    CC_INDEXED_INVERSE_DESTINATION_ALPHA
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
CcIndexedProgram *
cc_indexed_program_create(CcIndexedRenderer *renderer,
                          const CcIndexedProgramDescription *description, char *error,
                          size_t error_capacity);
CcIndexedMesh *cc_indexed_mesh_create(CcIndexedRenderer *renderer,
                                      const CcIndexedMeshDescription *description,
                                      char *error, size_t error_capacity);
/* Fixed-capacity dynamic meshes preserve the immutable index list and vertex
 * layout. Update replaces exactly the original vertex byte span, copying the
 * caller's data outside an active frame without application heap allocation.
 * Metal keeps an initial buffer per in-flight frame and updates a slot only
 * after its previous command completes. GLES2 uses core BufferSubData; driver
 * synchronization may block. No update changes a frame already submitted.
 * Ordinary immutable meshes reject updates. */
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
bool cc_indexed_draw(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                     char *error, size_t error_capacity);
bool cc_indexed_end(CcIndexedRenderer *renderer, char *error, size_t error_capacity);
/* Explicit synchronization for diagnostics or shutdown, outside active frames.
 * Ordinary frame submission does not call this blocking operation. */
bool cc_indexed_wait(CcIndexedRenderer *renderer, char *error, size_t error_capacity);

#endif
