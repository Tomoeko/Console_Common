#ifndef CC_SUPPORT_UTF8_H
#define CC_SUPPORT_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode one scalar value. On failure, offset and outputs are unchanged. */
bool cc_utf8_next(const uint8_t *bytes, size_t length, size_t *offset,
                  uint32_t *codepoint, size_t *utf16_units);
bool cc_utf8_valid(const uint8_t *bytes, size_t length);
bool cc_utf8_cstr_valid(const char *text);

#endif
