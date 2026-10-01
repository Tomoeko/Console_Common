#include "console_common/support/error.h"

#include <stdio.h>

void cc_error_set(char *error, size_t capacity, const char *message) {
    if (error && capacity)
        snprintf(error, capacity, "%s", message);
}
