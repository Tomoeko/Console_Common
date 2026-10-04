#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tool_io_internal.h"

char *cc_tool_handle_path(HANDLE handle) {
    DWORD size = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
    if (!size || size > 32767)
        return NULL;
    WCHAR *wide = malloc(((size_t)size + 1) * sizeof(*wide));
    char *path = NULL;
    if (wide) {
        DWORD length =
            GetFinalPathNameByHandleW(handle, wide, size + 1, FILE_NAME_NORMALIZED);
        if (length && length <= size)
            path = cc_windows_utf8(wide);
    }
    free(wide);
    if (path) {
        for (char *cursor = path; *cursor; ++cursor) {
            if (*cursor == '\\')
                *cursor = '/';
        }
    }
    return path;
}

char *cc_tool_child_path(int root, const char *name) {
    if (!name || !*name || strpbrk(name, "/\\:") || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || name[strlen(name) - 1] == '.' ||
        name[strlen(name) - 1] == ' ') {
        errno = EINVAL;
        return NULL;
    }
    char *parent = cc_tool_handle_path((HANDLE)_get_osfhandle(root));
    if (!parent)
        return NULL;
    size_t size = strlen(parent) + strlen(name) + 2;
    char *path = malloc(size);
    if (path)
        snprintf(path, size, "%s/%s", parent, name);
    free(parent);
    return path;
}

char *cc_tool_realpath(const char *path, char *result) {
    char *resolved = cc_windows_resolved_path(path);
    if (!resolved) {
        cc_tool_error(GetLastError());
        return NULL;
    }
    for (char *cursor = resolved; *cursor; ++cursor) {
        if (*cursor == '\\')
            *cursor = '/';
    }
    if (!result)
        return resolved;
    /* Callers supplying storage use the preparation path capacity. */
    if (strlen(resolved) >= 4096) {
        free(resolved);
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(result, resolved);
    free(resolved);
    return result;
}

size_t cc_tool_root_length(const char *path) {
    size_t start = 0;
    if (strncmp(path, "//?/UNC/", 8) == 0)
        start = 8;
    else if (strncmp(path, "//?/", 4) == 0)
        return strlen(path) >= 7 && path[5] == ':' ? 7 : 4;
    else if (strlen(path) >= 3 && path[1] == ':')
        return 3;
    else if (strncmp(path, "//", 2) == 0)
        start = 2;
    else
        return path[0] == '/' ? 1 : 0;
    const char *server = strchr(path + start, '/');
    const char *share = server ? strchr(server + 1, '/') : NULL;
    return share ? (size_t)(share - path) + 1 : strlen(path);
}

static bool move_path(const char *source, const char *destination, bool replace) {
    WCHAR *from = cc_windows_path(source);
    WCHAR *to = cc_windows_path(destination);
    bool okay =
        from && to &&
        MoveFileExW(from, to,
                    MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0));
    DWORD error = GetLastError();
    free(to);
    free(from);
    if (!okay)
        cc_tool_error(error);
    return okay;
}

int cc_tool_rename(const char *source, const char *destination) {
    return move_path(source, destination, true) ? 0 : -1;
}

bool cc_tool_publish(const char *source, const char *destination) {
    return move_path(source, destination, false);
}

static int delete_path(const char *path, bool directory) {
    WCHAR *wide = cc_windows_path(path);
    bool okay = wide && (directory ? RemoveDirectoryW(wide) : DeleteFileW(wide));
    DWORD error = GetLastError();
    free(wide);
    return okay ? 0 : cc_tool_error(error);
}

int cc_tool_unlink(const char *path) {
    return delete_path(path, false);
}

int cc_tool_rmdir(const char *path) {
    return delete_path(path, true);
}

int cc_tool_unlinkat(int root, const char *name, int flags) {
    if (flags) {
        errno = EINVAL;
        return -1;
    }
    char *path = cc_tool_child_path(root, name);
    int result = path ? cc_tool_unlink(path) : -1;
    free(path);
    return result;
}

int cc_tool_mkdir(const char *path, cc_tool_mode mode) {
    WCHAR *wide = cc_windows_path(path);
    SECURITY_ATTRIBUTES attributes = {0};
    PSECURITY_DESCRIPTOR descriptor = NULL;
    bool private = (mode & 0777) == 0700;
    bool okay = wide &&
                (!private || cc_tool_private_security(&attributes, &descriptor)) &&
                CreateDirectoryW(wide, private ? &attributes : NULL);
    DWORD error = GetLastError();
    LocalFree(descriptor);
    free(wide);
    return okay ? 0 : cc_tool_error(error);
}

int cc_tool_mkdirat(int root, const char *name, cc_tool_mode mode) {
    char *path = cc_tool_child_path(root, name);
    int result = path ? cc_tool_mkdir(path, mode) : -1;
    free(path);
    return result;
}

static bool random_suffix(char *path) {
    size_t length = strlen(path);
    if (length < 6 || strcmp(path + length - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return false;
    }
    uint8_t random[6];
    if (!cc_tool_random(random, sizeof(random)))
        return false;
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t index = 0; index < 6; ++index)
        path[length - 6 + index] = alphabet[random[index] % (sizeof(alphabet) - 1)];
    return true;
}

char *cc_tool_mkdtemp(char *path) {
    if (!random_suffix(path))
        return NULL;
    return cc_tool_mkdir(path, 0700) == 0 ? path : NULL;
}

int cc_tool_mkstemp(char *path) {
    if (!random_suffix(path))
        return -1;
    return cc_tool_open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
}
