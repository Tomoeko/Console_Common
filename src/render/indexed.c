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
        (unsigned)state->source_rgb > CC_INDEXED_CONSTANT_COLOR ||
        (unsigned)state->destination_rgb > CC_INDEXED_CONSTANT_COLOR ||
        (unsigned)state->source_alpha > CC_INDEXED_CONSTANT_COLOR ||
        (unsigned)state->destination_alpha > CC_INDEXED_CONSTANT_COLOR ||
        (unsigned)state->depth_compare > CC_INDEXED_ALWAYS ||
        (unsigned)state->cull > CC_INDEXED_CULL_BACK)
        return fail(error, error_capacity, "invalid indexed render state");
    for (size_t lane = 0; lane < 4; ++lane) {
        if (!isfinite(state->blend_color[lane]) || state->blend_color[lane] < 0.0f ||
            state->blend_color[lane] > 1.0f)
            return fail(error, error_capacity, "invalid indexed blend color");
    }
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
    if (!description || (uintptr_t)description % _Alignof(CcIndexedMeshDescription) ||
        sizeof(*description) > UINTPTR_MAX - (uintptr_t)description)
        return fail(error, error_capacity, "invalid indexed mesh description");
    if (!description->vertices || !description->indices || !description->vertex_count ||
        description->vertex_count > UINT16_MAX + 1u ||
        description->vertex_stride < sizeof(float) * 4 ||
        description->vertex_stride > INT_MAX ||
        description->vertex_stride % sizeof(float) ||
        description->vertex_count > PTRDIFF_MAX / description->vertex_stride ||
        !description->index_count || description->index_count > INT_MAX ||
        description->index_count % 3 ||
        description->index_count > PTRDIFF_MAX / sizeof(uint16_t))
        return fail(error, error_capacity, "invalid indexed mesh storage");
    size_t vertex_bytes = description->vertex_count * description->vertex_stride;
    size_t index_bytes = description->index_count * sizeof(uint16_t);
    if (vertex_bytes > UINTPTR_MAX - (uintptr_t)description->vertices ||
        (uintptr_t)description->indices % _Alignof(uint16_t) ||
        index_bytes > UINTPTR_MAX - (uintptr_t)description->indices)
        return fail(error, error_capacity, "invalid indexed mesh backing");
    for (size_t index = 0; index < description->index_count; ++index) {
        if (description->indices[index] >= description->vertex_count)
            return fail(error, error_capacity, "indexed mesh index out of range");
    }
    return true;
}

bool cc_indexed_mesh_update_validate(size_t expected, const void *vertices,
                                     size_t vertex_bytes, char *error,
                                     size_t error_capacity) {
    if (!vertices || !expected || vertex_bytes != expected ||
        vertex_bytes > UINTPTR_MAX - (uintptr_t)vertices)
        return fail(error, error_capacity, "invalid indexed mesh update span");
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
    return true;
}

bool cc_indexed_texture_layout(const CcIndexedTextureDescription *description,
                               CcIndexedTextureLayout *output, char *error,
                               size_t error_capacity) {
    if (!description ||
        (uintptr_t)description % _Alignof(CcIndexedTextureDescription) ||
        sizeof(*description) > UINTPTR_MAX - (uintptr_t)description || !output ||
        !cc_indexed_texture_validate(description, error, error_capacity))
        return fail(error, error_capacity, "invalid dynamic texture description");
    CcIndexedTextureLayout layout = {0};
    layout.level_count = description->level_count;
    for (size_t index = 0; index < layout.level_count; ++index) {
        const CcIndexedMip *level = &description->levels[index];
        if (level->size > UINTPTR_MAX - (uintptr_t)level->rgba ||
            level->size > SIZE_MAX - layout.byte_count)
            return fail(error, error_capacity, "dynamic texture storage overflows");
        layout.levels[index] = (CcIndexedTextureLevel){level->width, level->height,
                                                       layout.byte_count, level->size};
        layout.byte_count += level->size;
    }
    *output = layout;
    return true;
}

bool cc_indexed_texture_update_validate(const CcIndexedTextureLayout *layout,
                                        size_t level, const uint8_t *rgba,
                                        size_t byte_count, char *error,
                                        size_t error_capacity) {
    if (!layout || level >= layout->level_count || !rgba ||
        byte_count != layout->levels[level].byte_count ||
        byte_count > UINTPTR_MAX - (uintptr_t)rgba)
        return fail(error, error_capacity, "invalid dynamic texture update span");
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

bool cc_indexed_target_validate(const CcIndexedTargetDescription *description,
                                char *error, size_t error_capacity) {
    if (!description || !description->width || !description->height ||
        description->width > INT_MAX || description->height > INT_MAX ||
        (unsigned)description->color_format > CC_INDEXED_RGBA16_FLOAT ||
        (unsigned)description->min_filter > CC_INDEXED_LINEAR ||
        (unsigned)description->mag_filter > CC_INDEXED_LINEAR)
        return fail(error, error_capacity, "invalid indexed target description");
    size_t texel_bytes = description->color_format == CC_INDEXED_RGBA8 ? 4 : 8;
    if ((size_t)description->width > SIZE_MAX / texel_bytes ||
        (size_t)description->height >
            SIZE_MAX / ((size_t)description->width * texel_bytes))
        return fail(error, error_capacity, "indexed target size overflows storage");
    return true;
}

static bool pass_rectangle_valid(CcViewport rectangle, unsigned height, bool scissor) {
    if (rectangle.width < (scissor ? 0 : 1) || rectangle.height < (scissor ? 0 : 1) ||
        (scissor && (rectangle.x < 0 || rectangle.y < 0)))
        return false;
    /* The GLES2 bottom-origin request must remain exactly representable. */
    int64_t bottom = (int64_t)height - rectangle.y - rectangle.height;
    return bottom >= INT_MIN && bottom <= INT_MAX;
}

static bool pass_rectangles_valid(const CcIndexedPass *pass, unsigned width,
                                  unsigned height) {
    CcViewport viewport = pass->viewport;
    if (!pass_rectangle_valid(viewport, height, false))
        return false;
    if (pass->scissor_enabled)
        return pass_rectangle_valid(pass->scissor, height, true);
    return viewport.x >= 0 && viewport.y >= 0 && (unsigned)viewport.x <= width &&
           (unsigned)viewport.y <= height &&
           (unsigned)viewport.width <= width - (unsigned)viewport.x &&
           (unsigned)viewport.height <= height - (unsigned)viewport.y;
}

bool cc_indexed_pass_validate(const CcIndexedPass *pass, unsigned width,
                              unsigned height, bool depth_available, bool color_valid,
                              bool depth_valid, char *error, size_t error_capacity) {
    if (!pass || !width || !height || width > INT_MAX || height > INT_MAX ||
        !pass_rectangles_valid(pass, width, height) ||
        (unsigned)pass->color_load > CC_INDEXED_DISCARD ||
        (unsigned)pass->depth_load > CC_INDEXED_DISCARD ||
        (pass->depth_attachment && !depth_available) ||
        (!pass->depth_attachment &&
         (pass->depth_load == CC_INDEXED_CLEAR || pass->depth_full_write)) ||
        (pass->target && pass->color_load == CC_INDEXED_LOAD && !color_valid) ||
        (pass->target && pass->depth_attachment &&
         pass->depth_load == CC_INDEXED_LOAD && !depth_valid))
        return fail(error, error_capacity,
                    "invalid indexed pass attachment or viewport");
    CcIndexedFrame clear = {.clear_color = pass->clear_color,
                            .clear_depth = pass->clear_depth,
                            .clear_depth_enabled = pass->depth_load == CC_INDEXED_CLEAR,
                            .depth_attachment = pass->depth_attachment};
    return cc_indexed_frame_validate(&clear, error, error_capacity);
}
