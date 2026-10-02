#ifndef CC_FRAMEBUFFER_INTERNAL_H
#define CC_FRAMEBUFFER_INTERNAL_H

#include "console_common/platform/platform.h"

bool cc_framebuffer_storage(int width, int height, size_t alignment, size_t *stride,
                            size_t *byte_count);
bool cc_framebuffer_rgba(uint8_t *destination, size_t capacity, const uint8_t *source,
                         int width, int height, size_t source_stride, bool bgra,
                         bool bottom_up);

#endif
