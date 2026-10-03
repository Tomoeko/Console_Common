#include "indexed.h"

#include "console_common/support/error.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static bool fail(char *error, size_t capacity, const char *message) {
    cc_error_set(error, capacity, message);
    return false;
}

static bool names_validate(const char *const *names, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (!names[index] || !names[index][0])
            return false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (!strcmp(names[index], names[previous]))
                return false;
        }
    }
    return true;
}

bool cc_indexed_program_validate(const CcIndexedProgramDescription *description,
                                 char *error, size_t error_capacity) {
    if (!description || !description->attribute_count ||
        description->attribute_count > CC_INDEXED_ATTRIBUTES ||
        description->vertex_uniform_count > CC_INDEXED_UNIFORMS ||
        description->fragment_uniform_count > CC_INDEXED_UNIFORMS ||
        description->texture_count > CC_INDEXED_TEXTURES ||
        description->vertex_stride < sizeof(float) * 4 ||
        description->vertex_stride > INT_MAX ||
        description->vertex_stride % sizeof(float) ||
        description->metal_vertex_uniform_buffer == 0 ||
        description->metal_vertex_uniform_buffer > 30 ||
        description->metal_fragment_uniform_buffer > 30)
        return fail(error, error_capacity, "invalid indexed program layout");
    const CcIndexedState *state = &description->state;
    if ((unsigned)state->equation_rgb > CC_INDEXED_REVERSE_SUBTRACT ||
        (unsigned)state->equation_alpha > CC_INDEXED_REVERSE_SUBTRACT ||
        (unsigned)state->source_rgb > CC_INDEXED_INVERSE_DESTINATION_ALPHA ||
        (unsigned)state->destination_rgb > CC_INDEXED_INVERSE_DESTINATION_ALPHA ||
        (unsigned)state->source_alpha > CC_INDEXED_INVERSE_DESTINATION_ALPHA ||
        (unsigned)state->destination_alpha > CC_INDEXED_INVERSE_DESTINATION_ALPHA ||
        (unsigned)state->depth_compare > CC_INDEXED_ALWAYS ||
        (unsigned)state->cull > CC_INDEXED_CULL_BACK)
        return fail(error, error_capacity, "invalid indexed render state");
    for (size_t index = 0; index < description->attribute_count; ++index) {
        const CcIndexedAttribute *attribute = &description->attributes[index];
        if (!attribute->name || !attribute->name[0] ||
            attribute->offset % sizeof(float) ||
            attribute->offset > description->vertex_stride - sizeof(float) * 4)
            return fail(error, error_capacity, "invalid indexed attribute");
        for (size_t previous = 0; previous < index; ++previous) {
            if (!strcmp(attribute->name, description->attributes[previous].name))
                return fail(error, error_capacity, "duplicate indexed attribute");
        }
    }
    if (!names_validate(description->vertex_uniforms,
                        description->vertex_uniform_count) ||
        !names_validate(description->fragment_uniforms,
                        description->fragment_uniform_count))
        return fail(error, error_capacity, "invalid indexed uniform names");
    for (size_t vertex = 0; vertex < description->vertex_uniform_count; ++vertex) {
        for (size_t fragment = 0; fragment < description->fragment_uniform_count;
             ++fragment) {
            if (!strcmp(description->vertex_uniforms[vertex],
                        description->fragment_uniforms[fragment]))
                return fail(error, error_capacity,
                            "indexed stage uniform names must be distinct");
        }
    }
    for (size_t index = 0; index < description->texture_count; ++index) {
        const char *name = description->texture_uniforms[index];
        if (name && !name[0])
            return fail(error, error_capacity, "empty indexed texture name");
    }
    return true;
}

bool cc_indexed_mesh_validate(const CcIndexedMeshDescription *description, char *error,
                              size_t error_capacity) {
    if (!description || !description->vertices || !description->indices ||
        !description->vertex_count || description->vertex_count > UINT16_MAX + 1u ||
        description->vertex_stride < sizeof(float) * 4 ||
        description->vertex_stride > INT_MAX ||
        description->vertex_stride % sizeof(float) ||
        description->vertex_count > PTRDIFF_MAX / description->vertex_stride ||
        !description->index_count || description->index_count > INT_MAX ||
        description->index_count % 3 ||
        description->index_count > PTRDIFF_MAX / sizeof(uint16_t))
        return fail(error, error_capacity, "invalid indexed mesh storage");
    for (size_t index = 0; index < description->index_count; ++index) {
        if (description->indices[index] >= description->vertex_count)
            return fail(error, error_capacity, "indexed mesh index out of range");
    }
    return true;
}

bool cc_indexed_texture_validate(const CcIndexedTextureDescription *description,
                                 char *error, size_t error_capacity) {
    if (!description || !description->level_count ||
        description->level_count > CC_INDEXED_MIP_LEVELS ||
        (unsigned)description->min_filter > CC_INDEXED_LINEAR ||
        (unsigned)description->mag_filter > CC_INDEXED_LINEAR ||
        (unsigned)description->mip_filter > CC_INDEXED_MIP_LINEAR ||
        (unsigned)description->wrap_s > CC_INDEXED_MIRROR ||
        (unsigned)description->wrap_t > CC_INDEXED_MIRROR ||
        !isfinite(description->min_lod) || !isfinite(description->max_lod) ||
        description->min_lod < 0.0f || description->max_lod < description->min_lod ||
        description->max_anisotropy < 1 || description->max_anisotropy > 16)
        return fail(error, error_capacity, "invalid indexed texture description");
    unsigned width = description->levels[0].width;
    unsigned height = description->levels[0].height;
    for (size_t index = 0; index < description->level_count; ++index) {
        const CcIndexedMip *level = &description->levels[index];
        if (!level->rgba || !width || !height || width > INT_MAX || height > INT_MAX ||
            level->width != width || level->height != height ||
            (size_t)width > SIZE_MAX / 4 ||
            (size_t)height > SIZE_MAX / ((size_t)width * 4) ||
            level->size != (size_t)width * (size_t)height * 4)
            return fail(error, error_capacity, "invalid indexed mip dimensions");
        if (width == 1 && height == 1 && index + 1 < description->level_count)
            return fail(error, error_capacity, "indexed mip chain extends past 1x1");
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    const CcIndexedMip *last = &description->levels[description->level_count - 1];
    if (description->mip_filter != CC_INDEXED_MIP_NONE &&
        (last->width != 1 || last->height != 1))
        return fail(error, error_capacity, "indexed mip filter needs complete chain");
    return true;
}

bool cc_indexed_frame_validate(const CcIndexedFrame *frame, char *error,
                               size_t error_capacity) {
    if (!frame || !isfinite(frame->clear_color.r) || !isfinite(frame->clear_color.g) ||
        !isfinite(frame->clear_color.b) || !isfinite(frame->clear_color.a) ||
        !isfinite(frame->clear_depth) || frame->clear_depth < 0.0f ||
        frame->clear_depth > 1.0f ||
        (frame->clear_depth_enabled && !frame->depth_attachment))
        return fail(error, error_capacity, "invalid indexed frame clear");
    return true;
}

bool cc_indexed_uniforms_validate(const float (*rows)[4], size_t count, size_t expected,
                                  char *error, size_t error_capacity) {
    if (count != expected || count > CC_INDEXED_UNIFORMS || (count && !rows))
        return fail(error, error_capacity, "invalid indexed uniform rows");
    for (size_t row = 0; row < count; ++row) {
        for (size_t lane = 0; lane < 4; ++lane) {
            if (!isfinite(rows[row][lane]))
                return fail(error, error_capacity, "nonfinite indexed uniform");
        }
    }
    return true;
}
