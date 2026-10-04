#include "render/texture_source.h"
#include "file_util.h"

#include <stdlib.h>
#include <string.h>

intptr_t cc_texture_source_root_open(const char *path) {
    return (intptr_t)cc_windows_open_directory(path);
}

void cc_texture_source_root_close(intptr_t root_directory) {
    CloseHandle((HANDLE)root_directory);
}

FILE *cc_texture_source_open(intptr_t root_directory, const char *url,
                             size_t url_length) {
    size_t checked_length;
    if (root_directory == -1 || !cc_texture_source_url_valid(url, &checked_length) ||
        checked_length != url_length)
        return NULL;
    DWORD count = GetFinalPathNameByHandleW((HANDLE)root_directory, NULL, 0,
                                            FILE_NAME_NORMALIZED);
    WCHAR *wide_root =
        count && count < 32768 ? malloc(((size_t)count + 1) * sizeof(WCHAR)) : NULL;
    char *root = NULL;
    if (wide_root) {
        DWORD length = GetFinalPathNameByHandleW((HANDLE)root_directory, wide_root,
                                                 count + 1, FILE_NAME_NORMALIZED);
        if (length > 0 && length <= count)
            root = cc_windows_utf8(wide_root);
    }
    free(wide_root);
    if (!root)
        return NULL;
    size_t root_length = strlen(root);
    char *path = malloc(root_length + url_length + 3);
    if (!path) {
        free(root);
        return NULL;
    }
    memcpy(path, root, root_length);
    free(root);
    path[root_length] = '\\';
    memcpy(path + root_length + 1, url, url_length + 1);
    memcpy(path + root_length + 1 + url_length - 4, ".wmra", 6);
    HANDLE directories[512];
    size_t directory_count = 0;
    FILE *stream = NULL;
    for (size_t index = root_length + 1; path[index]; ++index) {
        if (path[index] != '/')
            continue;
        path[index] = '\0';
        HANDLE child = cc_windows_open_directory(path);
        path[index] = '\\';
        if (child == INVALID_HANDLE_VALUE || directory_count == 512) {
            if (child != INVALID_HANDLE_VALUE)
                CloseHandle(child);
            goto release_directories;
        }
        /* Parent handles exclude delete sharing, preventing path components
         * being renamed or replaced by reparse points while opening a leaf. */
        directories[directory_count++] = child;
    }
    HANDLE file = cc_windows_open_regular(path, GENERIC_READ, OPEN_EXISTING);
    if (file != INVALID_HANDLE_VALUE)
        stream = cc_windows_stream(file, "rb");
release_directories:
    while (directory_count)
        CloseHandle(directories[--directory_count]);
    free(path);
    return stream;
}
