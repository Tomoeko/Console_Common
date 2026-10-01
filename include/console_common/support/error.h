#ifndef CC_SUPPORT_ERROR_H
#define CC_SUPPORT_ERROR_H

#include <stddef.h>

/* An optional error buffer receives a truncated, NUL-terminated message. */
void cc_error_set(char *error, size_t capacity, const char *message);

#endif
