#include "console_common/resources/resource_tpl.h"
#include "console_common/resources/gx_texture.h"
#include "console_common/support/error.h"

#include "resource_bytes.h"

#include <stdlib.h>
#include <string.h>

enum {
    CC_TPL_MAX_IMAGES = 4096,
    /* A TPL may reuse one encoded image for many table entries. Bound the
     * combined RGBA output before allocating any decoded images. */
    CC_TPL_MAX_RGBA_BYTES = 256 * 1024 * 1024
};

static bool cc_read_palette(const uint8_t *data, size_t size, size_t header_offset,
                            uint8_t **palette, size_t *palette_count, char *error,
                            size_t error_size) {
    if (!cc_resource_range_fits(size, header_offset, 12)) {
        cc_error_set(error, error_size, "Truncated TPL palette header.");
        return false;
    }
    size_t count = cc_resource_be16(data + header_offset);
    uint32_t format = cc_resource_be32(data + header_offset + 4);
    size_t offset = cc_resource_be32(data + header_offset + 8);
    if (count == 0 || format > 2 || !cc_resource_range_fits(size, offset, count * 2)) {
        cc_error_set(error, error_size, "Invalid TPL palette.");
        return false;
    }

    uint8_t *colors = malloc(count * 4);
    if (colors == NULL) {
        cc_error_set(error, error_size, "Out of memory decoding TPL palette.");
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        uint16_t value = cc_resource_be16(data + offset + index * 2);
        uint8_t *color = colors + index * 4;
        if (format == 0) {
            color[0] = (uint8_t)value;
            color[1] = (uint8_t)value;
            color[2] = (uint8_t)value;
            color[3] = (uint8_t)(value >> 8);
        } else if (format == 1) {
            cc_gx_rgb565(value, CC_GX_COLOR_REPLICATE, color);
        } else {
            cc_gx_rgb5a3(value, CC_GX_COLOR_REPLICATE, color);
        }
    }
    *palette = colors;
    *palette_count = count;
    return true;
}

static bool cc_decode_image(const uint8_t *data, size_t size, size_t image_header,
                            size_t palette_header, CcTplImage *image, char *error,
                            size_t error_size) {
    if (!cc_resource_range_fits(size, image_header, 12)) {
        cc_error_set(error, error_size, "Truncated TPL image header.");
        return false;
    }
    uint16_t height = cc_resource_be16(data + image_header);
    uint16_t width = cc_resource_be16(data + image_header + 2);
    uint32_t format = cc_resource_be32(data + image_header + 4);
    size_t offset = cc_resource_be32(data + image_header + 8);
    size_t encoded_bytes;
    size_t rgba_bytes;
    if (!cc_gx_texture_size(format, width, height, &encoded_bytes, &rgba_bytes)) {
        cc_error_set(error, error_size, "Unsupported TPL image dimensions or format.");
        return false;
    }
    if (!cc_resource_range_fits(size, offset, encoded_bytes) ||
        rgba_bytes > CC_TPL_MAX_RGBA_BYTES) {
        cc_error_set(error, error_size, "TPL image data is truncated or too large.");
        return false;
    }

    uint8_t *palette = NULL;
    size_t palette_count = 0;
    if (format == 8 || format == 9 || format == 10) {
        if (palette_header == 0 ||
            !cc_read_palette(data, size, palette_header, &palette, &palette_count,
                             error, error_size)) {
            if (palette_header == 0) {
                cc_error_set(error, error_size, "Indexed TPL image has no palette.");
            }
            return false;
        }
    }

    uint8_t *rgba = malloc(rgba_bytes);
    if (rgba == NULL) {
        cc_error_set(error, error_size, "Out of memory decoding TPL image.");
        free(palette);
        return false;
    }

    CcGxTexture texture = {.width = width,
                           .height = height,
                           .format = format,
                           .pixels = data + offset,
                           .pixel_bytes = encoded_bytes,
                           .palette = palette,
                           .palette_count = palette_count,
                           .expansion = CC_GX_COLOR_REPLICATE};
    bool valid = cc_gx_texture_decode(&texture, rgba, rgba_bytes);
    if (!valid)
        cc_error_set(error, error_size, "Invalid TPL palette index.");
    free(palette);
    if (!valid) {
        free(rgba);
        return false;
    }
    *image = (CcTplImage){width, height, format, rgba};
    return true;
}

void cc_tpl_free(CcTpl *tpl) {
    if (tpl == NULL) {
        return;
    }
    for (size_t index = 0; index < tpl->count; index++) {
        free(tpl->images[index].rgba);
    }
    free(tpl->images);
    *tpl = (CcTpl){0};
}

bool cc_tpl_decode(const uint8_t *data, size_t size, CcTpl *tpl, char *error,
                   size_t error_size) {
    if (data == NULL || tpl == NULL || size < 12 ||
        cc_resource_be32(data) != 0x0020af30u) {
        cc_error_set(error, error_size, "Expected a TPL texture archive.");
        return false;
    }
    *tpl = (CcTpl){0};

    size_t count = cc_resource_be32(data + 4);
    size_t table = cc_resource_be32(data + 8);
    if (count == 0 || count > CC_TPL_MAX_IMAGES ||
        !cc_resource_range_fits(size, table, count * 8)) {
        cc_error_set(error, error_size, "Invalid TPL texture table.");
        return false;
    }

    uint64_t total_rgba_bytes = 0;
    for (size_t index = 0; index < count; index++) {
        size_t image_header = cc_resource_be32(data + table + index * 8);
        if (!cc_resource_range_fits(size, image_header, 12)) {
            cc_error_set(error, error_size, "Truncated TPL image header.");
            return false;
        }
        uint64_t height = cc_resource_be16(data + image_header);
        uint64_t width = cc_resource_be16(data + image_header + 2);
        uint64_t image_bytes = width * height * 4;
        if (image_bytes > CC_TPL_MAX_RGBA_BYTES - total_rgba_bytes) {
            cc_error_set(error, error_size,
                         "TPL decoded images exceed the memory limit.");
            return false;
        }
        total_rgba_bytes += image_bytes;
    }

    tpl->images = calloc(count, sizeof(*tpl->images));
    if (tpl->images == NULL) {
        cc_error_set(error, error_size, "Out of memory decoding TPL archive.");
        return false;
    }
    tpl->count = count;
    for (size_t index = 0; index < count; index++) {
        size_t image_header = cc_resource_be32(data + table + index * 8);
        size_t palette_header = cc_resource_be32(data + table + index * 8 + 4);
        if (!cc_decode_image(data, size, image_header, palette_header,
                             &tpl->images[index], error, error_size)) {
            cc_tpl_free(tpl);
            return false;
        }
    }
    return true;
}
