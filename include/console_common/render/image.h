#ifndef CONSOLE_COMMON_IMAGE_H
#define CONSOLE_COMMON_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Versioned, backend-neutral RGBA8 resource. Generated files belong in the
 * ignored local asset directory, never in tracked source. */
typedef struct CcImage {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} CcImage;

bool cc_image_read(const char *path, CcImage *image);
bool cc_image_write(const char *path, const CcImage *image);
void cc_image_free(CcImage *image);

#endif
