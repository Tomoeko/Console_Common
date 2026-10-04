#include "console_common/support/regular_file.h"
#include "file_util.h"

#include <stdint.h>
#include <stdlib.h>

static CcRegularFileStatus read_range(const char *path, size_t minimum, size_t limit,
                                      uint8_t **contents, size_t *length) {
    if (!contents || !length)
        return CC_REGULAR_FILE_ERROR;
    *contents = NULL;
    *length = 0;
    if (!path || minimum > limit)
        return CC_REGULAR_FILE_ERROR;
    HANDLE file = cc_windows_open_regular(path, GENERIC_READ, OPEN_EXISTING);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? CC_REGULAR_FILE_MISSING
                   : CC_REGULAR_FILE_ERROR;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart < minimum || (uint64_t)size.QuadPart > limit ||
        (uint64_t)size.QuadPart >= SIZE_MAX) {
        CloseHandle(file);
        return CC_REGULAR_FILE_ERROR;
    }
    size_t count = (size_t)size.QuadPart;
    uint8_t *bytes = malloc(count + 1);
    if (!bytes) {
        CloseHandle(file);
        return CC_REGULAR_FILE_ERROR;
    }
    size_t offset = 0;
    bool okay = true;
    while (offset < count) {
        DWORD request = count - offset > MAXDWORD ? MAXDWORD : (DWORD)(count - offset);
        DWORD read = 0;
        if (!ReadFile(file, bytes + offset, request, &read, NULL) || !read) {
            okay = false;
            break;
        }
        offset += read;
    }
    DWORD extra = 0;
    uint8_t after;
    okay = okay && ReadFile(file, &after, 1, &extra, NULL) && extra == 0;
    if (!CloseHandle(file))
        okay = false;
    if (!okay) {
        free(bytes);
        return CC_REGULAR_FILE_ERROR;
    }
    bytes[count] = 0;
    *contents = bytes;
    *length = count;
    return CC_REGULAR_FILE_OK;
}

CcRegularFileStatus cc_regular_file_read(const char *path, size_t limit,
                                         char **contents, size_t *length) {
    if (!contents)
        return CC_REGULAR_FILE_ERROR;
    uint8_t *bytes = NULL;
    CcRegularFileStatus status = read_range(path, 1, limit, &bytes, length);
    *contents = (char *)bytes;
    return status;
}

CcRegularFileStatus cc_regular_file_read_bytes(const char *path, size_t minimum,
                                               size_t limit, uint8_t **contents,
                                               size_t *length) {
    return read_range(path, minimum, limit, contents, length);
}
