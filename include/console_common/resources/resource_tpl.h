#ifndef CONSOLE_COMMON_RESOURCE_TPL_H
#define CONSOLE_COMMON_RESOURCE_TPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RGBA8 rows begin at the image's top edge, ready for upload to the graphics
 * backend. The decoded TPL owns each image's pixels. */
typedef struct CcTplImage {
    uint16_t width;
    uint16_t height;
    uint32_t format;
    uint8_t *rgba;
} CcTplImage;

typedef struct CcTpl {
    CcTplImage *images;
    size_t count;
} CcTpl;

bool cc_tpl_decode(const uint8_t *data, size_t size, CcTpl *tpl, char *error,
                   size_t error_size);
void cc_tpl_free(CcTpl *tpl);

#endif
