#include "file_util.h"

#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

WCHAR *cc_windows_wide(const char *text) {
    if (!text)
        return NULL;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(WCHAR))
        return NULL;
    WCHAR *wide = malloc((size_t)count * sizeof(*wide));
    if (!wide)
        return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}

WCHAR *cc_windows_path(const char *path) {
    WCHAR *wide = cc_windows_wide(path);
    if (wide) {
        size_t used = 0;
        for (size_t index = 0; wide[index]; ++index) {
            WCHAR value = wide[index] == L'/' ? L'\\' : wide[index];
            if (value == L'\\' && used > 1 && wide[used - 1] == L'\\')
                continue;
            wide[used++] = value;
        }
        size_t root =
            used >= 7 && !wcsncmp(wide, L"\\\\?\\", 4) && wide[5] == L':' ? 7 : 3;
        while (used > root && wide[used - 1] == L'\\')
            --used;
        wide[used] = L'\0';
    }
    return wide;
}

char *cc_windows_utf8(const WCHAR *text) {
    if (!text)
        return NULL;
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0,
                                    NULL, NULL);
    if (count <= 0)
        return NULL;
    char *utf8 = malloc((size_t)count);
    if (!utf8)
        return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8, count, NULL,
                             NULL)) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static HANDLE open_object(const char *path, DWORD access, DWORD creation,
                          bool directory) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    HANDLE handle = CreateFileW(wide, access, FILE_SHARE_READ, NULL, creation,
                                FILE_FLAG_OPEN_REPARSE_POINT |
                                    (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),
                                NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return handle;
    BY_HANDLE_FILE_INFORMATION information;
    if (!GetFileInformationByHandle(handle, &information) ||
        GetFileType(handle) != FILE_TYPE_DISK ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
        CloseHandle(handle);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

HANDLE cc_windows_open_regular(const char *path, DWORD access, DWORD creation) {
    return open_object(path, access, creation, false);
}

HANDLE cc_windows_open_directory(const char *path) {
    return open_object(path, GENERIC_READ, OPEN_EXISTING, true);
}

FILE *cc_windows_stream(HANDLE handle, const char *mode) {
    int flags = strchr(mode, 'w') ? _O_WRONLY : _O_RDONLY;
    int descriptor = _open_osfhandle((intptr_t)handle, flags | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(handle);
        return NULL;
    }
    FILE *stream = _fdopen(descriptor, mode);
    if (!stream)
        _close(descriptor);
    return stream;
}

char *cc_windows_resolved_path(const char *path) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide)
        return NULL;
    HANDLE handle =
        CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    DWORD count = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
    WCHAR *resolved =
        count && count < 32768 ? malloc(((size_t)count + 1) * sizeof(WCHAR)) : NULL;
    char *utf8 = NULL;
    if (resolved) {
        DWORD length = GetFinalPathNameByHandleW(handle, resolved, count + 1,
                                                 FILE_NAME_NORMALIZED);
        if (length > 0 && length <= count)
            utf8 = cc_windows_utf8(resolved);
    }
    free(resolved);
    CloseHandle(handle);
    return utf8;
}

bool cc_windows_create_directory(const char *path) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide)
        return false;
    bool okay =
        CreateDirectoryW(wide, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
    free(wide);
    HANDLE directory = okay ? cc_windows_open_directory(path) : INVALID_HANDLE_VALUE;
    if (directory == INVALID_HANDLE_VALUE)
        return false;
    return CloseHandle(directory) != 0;
}

bool cc_windows_path_missing(const char *path) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide)
        return false;
    DWORD attributes = GetFileAttributesW(wide);
    DWORD error = GetLastError();
    free(wide);
    return attributes == INVALID_FILE_ATTRIBUTES &&
           (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}
