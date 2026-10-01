#ifndef CC_RESOURCE_BYTES_H
#define CC_RESOURCE_BYTES_H

#include "console_common/support/bounds.h"
#include "console_common/support/endian.h"

static inline bool cc_resource_range_fits(size_t size, size_t offset, size_t length) {
    return cc_bounds_contains(size, offset, length);
}

/* Check the containing range before reading from either pointer. */
static inline uint16_t cc_resource_be16(const uint8_t *bytes) {
    return cc_read_be16(bytes);
}

static inline uint32_t cc_resource_be32(const uint8_t *bytes) {
    return cc_read_be32(bytes);
}

#endif
