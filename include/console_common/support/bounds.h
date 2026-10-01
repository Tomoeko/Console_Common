#ifndef CC_SUPPORT_BOUNDS_H
#define CC_SUPPORT_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>

/* Subtraction after the offset check avoids overflow in offset + length. */
static inline bool cc_bounds_contains(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

#endif
