#include "texture_dimensions.h"

#include <assert.h>

int main(void) {
    CcGles2TextureDimensions sizes = {0};
    int dimensions[2] = {7, 9};
    assert(!cc_gles2_texture_dimensions_get(&sizes, 1, dimensions));
    assert(dimensions[0] == 7 && dimensions[1] == 9);
    assert(!cc_gles2_texture_dimensions_set(&sizes, 0, 2, 1));
    assert(!cc_gles2_texture_dimensions_set(&sizes, 1, 0, 1));
    assert(!cc_gles2_texture_dimensions_set(NULL, 1, 1, 1));
    assert(!sizes.count && !sizes.entries);
    assert(cc_gles2_texture_dimensions_set(&sizes, UINT32_MAX, 2, 1));
    for (uint32_t handle = 1000; handle; --handle)
        assert(cc_gles2_texture_dimensions_set(&sizes, handle, (int)handle, 8));
    for (uint32_t handle = 1; handle <= 1000; ++handle) {
        assert(cc_gles2_texture_dimensions_get(&sizes, handle, dimensions));
        assert(dimensions[0] == (int)handle && dimensions[1] == 8);
    }
    assert(cc_gles2_texture_dimensions_set_nearest(&sizes, 500, UINT32_MAX - 1));
    assert(!cc_gles2_texture_dimensions_set_nearest(&sizes, 500, 500));
    assert(!cc_gles2_texture_dimensions_set_nearest(&sizes, 2000, 3000));
    assert(!cc_gles2_texture_dimensions_set_nearest(NULL, 500, 100));
    assert(cc_gles2_texture_dimensions_set(&sizes, 500, 8, 16));
    assert(cc_gles2_texture_dimensions_nearest(&sizes, 500) == UINT32_MAX - 1);
    assert(sizes.count == 1001);
    assert(cc_gles2_texture_dimensions_get(&sizes, 500, dimensions));
    assert(dimensions[0] == 8 && dimensions[1] == 16);
    cc_gles2_texture_dimensions_remove(&sizes, 500);
    assert(!cc_gles2_texture_dimensions_get(&sizes, 500, dimensions));
    assert(!cc_gles2_texture_dimensions_nearest(&sizes, 500));
    assert(dimensions[0] == 8 && dimensions[1] == 16);
    assert(cc_gles2_texture_dimensions_set(&sizes, 500, 1, 1));
    assert(!cc_gles2_texture_dimensions_nearest(&sizes, 500));
    assert(cc_gles2_texture_dimensions_set_nearest(&sizes, 500, 501));
    assert(cc_gles2_texture_dimensions_set_nearest(&sizes, 500, 0));
    assert(!cc_gles2_texture_dimensions_nearest(&sizes, 500));
    cc_gles2_texture_dimensions_remove(&sizes, UINT32_MAX);
    assert(!cc_gles2_texture_dimensions_get(&sizes, UINT32_MAX, dimensions));
    cc_gles2_texture_dimensions_destroy(&sizes);
    assert(!sizes.count && !sizes.capacity && !sizes.entries);
    cc_gles2_texture_dimensions_destroy(&sizes);
    assert(cc_gles2_texture_dimensions_set(&sizes, 1, 1, 1));
    cc_gles2_texture_dimensions_destroy(&sizes);
    return 0;
}
