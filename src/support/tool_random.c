#define _POSIX_C_SOURCE 200809L

#include "console_common/support/tool_io.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

bool cc_tool_random(void *bytes, size_t size) {
    return size <= ULONG_MAX && BCryptGenRandom(NULL, bytes, (ULONG)size,
                                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
#else
#include <errno.h>

bool cc_tool_random(void *bytes, size_t size) {
    int file = open("/dev/urandom", O_RDONLY);
    if (file < 0)
        return false;
    size_t used = 0;
    while (used < size) {
        ssize_t count = read(file, (uint8_t *)bytes + used, size - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    return close(file) == 0 && used == size;
}
#endif
