#include "texture_dimensions.h"

#include <stdlib.h>
#include <string.h>

static size_t entry_index(const CcGles2TextureDimensions *sizes, uint32_t handle) {
    size_t first = 0, last = sizes->count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (sizes->entries[middle].handle < handle)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

bool cc_gles2_texture_dimensions_set(CcGles2TextureDimensions *sizes, uint32_t handle,
                                     int width, int height) {
    if (!sizes || !handle || width <= 0 || height <= 0)
        return false;
    size_t index = entry_index(sizes, handle);
    if (index < sizes->count && sizes->entries[index].handle == handle) {
        sizes->entries[index].width = width;
        sizes->entries[index].height = height;
        return true;
    }
    if (sizes->count == sizes->capacity) {
        size_t capacity = sizes->capacity ? sizes->capacity * 2 : 16;
        if (capacity < sizes->capacity || capacity > SIZE_MAX / sizeof(*sizes->entries))
            return false;
        CcGles2TextureSize *entries =
            realloc(sizes->entries, capacity * sizeof(*entries));
        if (!entries)
            return false;
        sizes->entries = entries;
        sizes->capacity = capacity;
    }
    memmove(sizes->entries + index + 1, sizes->entries + index,
            (sizes->count - index) * sizeof(*sizes->entries));
    sizes->entries[index] = (CcGles2TextureSize){handle, width, height, 0};
    ++sizes->count;
    return true;
}

bool cc_gles2_texture_dimensions_get(const CcGles2TextureDimensions *sizes,
                                     uint32_t handle, int dimensions[2]) {
    if (!sizes || !handle || !dimensions)
        return false;
    size_t index = entry_index(sizes, handle);
    if (index == sizes->count || sizes->entries[index].handle != handle)
        return false;
    dimensions[0] = sizes->entries[index].width;
    dimensions[1] = sizes->entries[index].height;
    return true;
}

bool cc_gles2_texture_dimensions_set_nearest(CcGles2TextureDimensions *sizes,
                                             uint32_t handle, uint32_t nearest_handle) {
    if (!sizes || !handle || handle == nearest_handle)
        return false;
    size_t index = entry_index(sizes, handle);
    if (index == sizes->count || sizes->entries[index].handle != handle)
        return false;
    sizes->entries[index].nearest_handle = nearest_handle;
    return true;
}

uint32_t cc_gles2_texture_dimensions_nearest(const CcGles2TextureDimensions *sizes,
                                             uint32_t handle) {
    if (!sizes || !handle)
        return 0;
    size_t index = entry_index(sizes, handle);
    return index < sizes->count && sizes->entries[index].handle == handle
               ? sizes->entries[index].nearest_handle
               : 0;
}

void cc_gles2_texture_dimensions_remove(CcGles2TextureDimensions *sizes,
                                        uint32_t handle) {
    if (!sizes || !handle)
        return;
    size_t index = entry_index(sizes, handle);
    if (index == sizes->count || sizes->entries[index].handle != handle)
        return;
    --sizes->count;
    memmove(sizes->entries + index, sizes->entries + index + 1,
            (sizes->count - index) * sizeof(*sizes->entries));
}

void cc_gles2_texture_dimensions_destroy(CcGles2TextureDimensions *sizes) {
    if (sizes) {
        free(sizes->entries);
        *sizes = (CcGles2TextureDimensions){0};
    }
}
