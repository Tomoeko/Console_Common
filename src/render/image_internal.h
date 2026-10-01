#ifndef CC_RENDER_IMAGE_INTERNAL_H
#define CC_RENDER_IMAGE_INTERNAL_H

#include "console_common/render/image.h"

#include <stddef.h>
#include <stdio.h>

/* Reject an oversized header before allocating its pixel payload. */
bool cc_image_read_bounded(const char *path, size_t maximum_bytes, CcImage *image);
bool cc_image_read_bounded_stream(FILE *stream, size_t maximum_bytes, CcImage *image);

/* Reads a valid header and restores the stream to its start for decoding. */
bool cc_image_declared_bytes(FILE *stream, size_t *bytes);

#endif
