#include "console_common/render/image.h"
#include "console_common/support/endian.h"

#include "../support/atomic_file.h"
#include "image_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CC_IMAGE_HEADER_SIZE = 16, CC_IMAGE_MAX_DIMENSION = 8192 };

static bool image_size(uint32_t width, uint32_t height, size_t *size) {
    if (!width || !height || width > CC_IMAGE_MAX_DIMENSION ||
        height > CC_IMAGE_MAX_DIMENSION)
        return false;
    uint64_t count = (uint64_t)width * height * 4;
    if (count > 256ULL * 1024 * 1024 || count > SIZE_MAX)
        return false;
    *size = (size_t)count;
    return true;
}

static bool read_image_header(FILE *stream, uint32_t *width, uint32_t *height,
                              size_t *size) {
    uint8_t header[CC_IMAGE_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
        memcmp(header, "WMRA", 4) != 0 || cc_read_le32(header + 4) != 1) {
        return false;
    }
    *width = cc_read_le32(header + 8);
    *height = cc_read_le32(header + 12);
    return image_size(*width, *height, size);
}

bool cc_image_declared_bytes(FILE *stream, size_t *bytes) {
    if (!stream || !bytes) {
        return false;
    }
    *bytes = 0;
    uint32_t width;
    uint32_t height;
    size_t size;
    if (!read_image_header(stream, &width, &height, &size) ||
        fseek(stream, 0, SEEK_SET) != 0) {
        return false;
    }
    *bytes = size;
    return true;
}

bool cc_image_read_bounded_stream(FILE *stream, size_t maximum_bytes, CcImage *image) {
    if (!stream || !image) {
        return false;
    }
    memset(image, 0, sizeof(*image));
    size_t size = 0;
    bool valid = read_image_header(stream, &image->width, &image->height, &size) &&
                 size <= maximum_bytes;
    if (valid) {
        image->pixels = malloc(size);
        valid = image->pixels && fread(image->pixels, 1, size, stream) == size &&
                fgetc(stream) == EOF && !ferror(stream);
    }
    if (!valid) {
        cc_image_free(image);
    }
    return valid;
}

bool cc_image_read_bounded(const char *path, size_t maximum_bytes, CcImage *image) {
    if (!path || !image) {
        return false;
    }
    memset(image, 0, sizeof(*image));
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        return false;
    }
    bool valid = cc_image_read_bounded_stream(stream, maximum_bytes, image);
    fclose(stream);
    return valid;
}

bool cc_image_read(const char *path, CcImage *image) {
    return cc_image_read_bounded(path, SIZE_MAX, image);
}

bool cc_image_write(const char *path, const CcImage *image) {
    if (!path || !image || !image->pixels)
        return false;
    size_t size;
    if (!image_size(image->width, image->height, &size))
        return false;
    uint8_t header[CC_IMAGE_HEADER_SIZE] = {'W', 'M', 'R', 'A'};
    cc_write_le32(header + 4, 1);
    cc_write_le32(header + 8, image->width);
    cc_write_le32(header + 12, image->height);
    CcAtomicFile output;
    if (cc_atomic_file_open(&output, path) != CC_ATOMIC_FILE_OK)
        return false;
    bool success = fwrite(header, 1, sizeof(header), output.stream) == sizeof(header) &&
                   fwrite(image->pixels, 1, size, output.stream) == size;
    if (!success) {
        cc_atomic_file_discard(&output);
        return false;
    }
    return cc_atomic_file_commit(&output, path);
}

void cc_image_free(CcImage *image) {
    if (!image)
        return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
