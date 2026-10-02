#ifndef CC_SUPPORT_ENDIAN_H
#define CC_SUPPORT_ENDIAN_H

#include <float.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
                   FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Native resource floats require IEEE 754 binary32.");

/* Callers must verify that 2, 4, or 8 bytes remain before access. */
static inline uint16_t cc_read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static inline uint32_t cc_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static inline uint64_t cc_read_be64(const uint8_t *bytes) {
    return ((uint64_t)cc_read_be32(bytes) << 32) | cc_read_be32(bytes + 4);
}

/* Native resource floats store an IEEE 754 binary32 word in byte order. */
static inline float cc_read_be_float(const uint8_t *bytes) {
    uint32_t bits = cc_read_be32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint32_t cc_read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline void cc_write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static inline void cc_write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static inline void cc_write_be64(uint8_t *bytes, uint64_t value) {
    cc_write_be32(bytes, (uint32_t)(value >> 32));
    cc_write_be32(bytes + 4, (uint32_t)value);
}

static inline void cc_write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

#endif
