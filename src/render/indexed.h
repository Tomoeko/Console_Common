#ifndef CC_RENDER_INDEXED_H
#define CC_RENDER_INDEXED_H

#include "console_common/platform/indexed.h"

bool cc_indexed_program_validate(const CcIndexedProgramDescription *description,
                                 char *error, size_t error_capacity);
bool cc_indexed_mesh_validate(const CcIndexedMeshDescription *description, char *error,
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
