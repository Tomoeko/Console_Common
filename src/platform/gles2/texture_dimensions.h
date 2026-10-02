#ifndef CONSOLE_COMMON_GLES2_TEXTURE_DIMENSIONS_H
#define CONSOLE_COMMON_GLES2_TEXTURE_DIMENSIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcGles2TextureSize {
    uint32_t handle;
    int width;
    int height;
    uint32_t nearest_handle;
} CcGles2TextureSize;

typedef struct CcGles2TextureDimensions {
    CcGles2TextureSize *entries;
    size_t count;
    size_t capacity;
} CcGles2TextureDimensions;

/* Zero initialization creates an empty registry. Allocation occurs only when
 * registering uploads; draws perform a bounded lookup without allocating. */
bool cc_gles2_texture_dimensions_set(CcGles2TextureDimensions *sizes, uint32_t handle,
                                     int width, int height);
bool cc_gles2_texture_dimensions_get(const CcGles2TextureDimensions *sizes,
                                     uint32_t handle, int dimensions[2]);
/* The backend owns both GPU handles and deletes the mirror before removal.
 * A zero mirror selects the ordinary texture; setting it never allocates. */
bool cc_gles2_texture_dimensions_set_nearest(CcGles2TextureDimensions *sizes,
                                             uint32_t handle, uint32_t nearest_handle);
uint32_t cc_gles2_texture_dimensions_nearest(const CcGles2TextureDimensions *sizes,
                                             uint32_t handle);
void cc_gles2_texture_dimensions_remove(CcGles2TextureDimensions *sizes,
                                        uint32_t handle);
void cc_gles2_texture_dimensions_destroy(CcGles2TextureDimensions *sizes);

#endif
