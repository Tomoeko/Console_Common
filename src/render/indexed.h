#ifndef CC_RENDER_INDEXED_H
#define CC_RENDER_INDEXED_H

#include "console_common/platform/indexed.h"

typedef struct CcIndexedTextureLevel {
    unsigned width;
    unsigned height;
    size_t offset;
    size_t byte_count;
} CcIndexedTextureLevel;

typedef struct CcIndexedTextureLayout {
    CcIndexedTextureLevel levels[CC_INDEXED_MIP_LEVELS];
    size_t level_count;
    size_t byte_count;
} CcIndexedTextureLayout;

bool cc_indexed_texture_layout(const CcIndexedTextureDescription *description,
                               CcIndexedTextureLayout *output, char *error,
                               size_t error_capacity);
bool cc_indexed_texture_update_validate(const CcIndexedTextureLayout *layout,
                                        size_t level, const uint8_t *rgba,
                                        size_t byte_count, char *error,
                                        size_t error_capacity);

bool cc_indexed_program_validate(const CcIndexedProgramDescription *description,
                                 char *error, size_t error_capacity);
bool cc_indexed_mesh_validate(const CcIndexedMeshDescription *description, char *error,
                              size_t error_capacity);
bool cc_indexed_mesh_update_validate(size_t expected, const void *vertices,
                                     size_t vertex_bytes, char *error,
                                     size_t error_capacity);
bool cc_indexed_texture_validate(const CcIndexedTextureDescription *description,
                                 char *error, size_t error_capacity);
bool cc_indexed_frame_validate(const CcIndexedFrame *frame, char *error,
                               size_t error_capacity);
bool cc_indexed_target_validate(const CcIndexedTargetDescription *description,
                                char *error, size_t error_capacity);
bool cc_indexed_pass_validate(const CcIndexedPass *pass, unsigned width,
                              unsigned height, bool depth_available, bool color_valid,
                              bool depth_valid, char *error, size_t error_capacity);
bool cc_indexed_uniforms_validate(const float (*rows)[4], size_t count, size_t expected,
                                  char *error, size_t error_capacity);

#endif
