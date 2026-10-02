#ifndef CC_CAPTURE_JPEG_H
#define CC_CAPTURE_JPEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcCaptureJpeg CcCaptureJpeg;

/* Baseline JPEG keeps all three colour planes at the source pixel resolution.
 * Quality is 1..100; 97 is the recording default. The owner allocates reusable
 * output and comparison storage once. Samples are borrowed until next encode.
 */
CcCaptureJpeg *cc_capture_jpeg_open(unsigned width, unsigned height, unsigned quality);
bool cc_capture_jpeg_encode(CcCaptureJpeg *encoder, const uint8_t *rgba,
                            size_t row_stride, const uint8_t **sample, size_t *size);
void cc_capture_jpeg_close(CcCaptureJpeg *encoder);

#endif
