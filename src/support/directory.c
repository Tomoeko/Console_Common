#define _POSIX_C_SOURCE 200809L
#include "console_common/support/directory.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "platform/windows/file_util.h"

struct CcDirectory {
    HANDLE directory;
    HANDLE search;
    WIN32_FIND_DATAW entry;
    bool first;
};

CcPathKind cc_path_information(const char *path, bool follow_links, uint64_t *size) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide)
        return CC_PATH_ERROR;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow_links)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    HANDLE file =
        CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, flags, NULL);
    DWORD error = GetLastError();
    free(wide);
    if (file == INVALID_HANDLE_VALUE)
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? CC_PATH_MISSING
                   : CC_PATH_ERROR;
    BY_HANDLE_FILE_INFORMATION information;
    CcPathKind kind = CC_PATH_ERROR;
    if (GetFileInformationByHandle(file, &information)) {
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            GetFileType(file) != FILE_TYPE_DISK) {
            kind = CC_PATH_OTHER;
        } else {
            kind = information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
                       ? CC_PATH_DIRECTORY
                       : CC_PATH_FILE;
            if (size)
                *size = (uint64_t)information.nFileSizeHigh << 32 |
                        information.nFileSizeLow;
        }
    }
    CloseHandle(file);
    return kind;
}

bool cc_directory_create(const char *path) {
    return cc_windows_create_directory(path);
}

CcDirectory *cc_directory_open(const char *path) {
    HANDLE handle = cc_windows_open_directory(path);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    CcDirectory *directory = calloc(1, sizeof(*directory));
    size_t length = strlen(path);
    char *pattern = length <= SIZE_MAX - 3 ? malloc(length + 3) : NULL;
    WCHAR *wide = NULL;
    if (pattern) {
        memcpy(pattern, path, length);
        memcpy(pattern + length, "/*", 3);
        wide = cc_windows_path(pattern);
    }
    free(pattern);
    if (!directory || !wide) {
        free(directory);
        free(wide);
        CloseHandle(handle);
        return NULL;
    }
    directory->directory = handle;
    directory->search = FindFirstFileW(wide, &directory->entry);
    DWORD error = GetLastError();
    free(wide);
    directory->first = directory->search != INVALID_HANDLE_VALUE;
    if (!directory->first && error != ERROR_FILE_NOT_FOUND) {
        cc_directory_close(directory);
        return NULL;
    }
    return directory;
}

int cc_directory_next(CcDirectory *directory, char *name, size_t capacity) {
    if (!directory || !name || !capacity)
        return -1;
    if (directory->search == INVALID_HANDLE_VALUE)
        return 0;
    if (!directory->first && !FindNextFileW(directory->search, &directory->entry))
        return GetLastError() == ERROR_NO_MORE_FILES ? 0 : -1;
    directory->first = false;
    char *utf8 = cc_windows_utf8(directory->entry.cFileName);
    if (!utf8)
        return -1;
    size_t length = strlen(utf8);
    if (length < capacity)
        memcpy(name, utf8, length + 1);
    free(utf8);
    return length < capacity ? 1 : -1;
}

void cc_directory_close(CcDirectory *directory) {
    if (!directory)
        return;
    if (directory->search != INVALID_HANDLE_VALUE)
        FindClose(directory->search);
    CloseHandle(directory->directory);
    free(directory);
}

#else
#include <dirent.h>
#include <sys/stat.h>

struct CcDirectory {
    DIR *directory;
};

CcPathKind cc_path_information(const char *path, bool follow_links, uint64_t *size) {
    struct stat information;
    int status = follow_links ? stat(path, &information) : lstat(path, &information);
    if (status != 0)
        return errno == ENOENT ? CC_PATH_MISSING : CC_PATH_ERROR;
    if (size)
        *size = information.st_size >= 0 ? (uint64_t)information.st_size : 0;
    return S_ISDIR(information.st_mode)   ? CC_PATH_DIRECTORY
           : S_ISREG(information.st_mode) ? CC_PATH_FILE
                                          : CC_PATH_OTHER;
}

bool cc_directory_create(const char *path) {
    return mkdir(path, 0700) == 0 ||
           (errno == EEXIST &&
            cc_path_information(path, false, NULL) == CC_PATH_DIRECTORY);
}

CcDirectory *cc_directory_open(const char *path) {
    DIR *entries = opendir(path);
    if (!entries)
        return NULL;
    CcDirectory *directory = malloc(sizeof(*directory));
    if (!directory) {
        closedir(entries);
        return NULL;
    }
    directory->directory = entries;
    return directory;
}

int cc_directory_next(CcDirectory *directory, char *name, size_t capacity) {
    if (!directory || !name || !capacity)
        return -1;
    errno = 0;
    struct dirent *entry = readdir(directory->directory);
    if (!entry)
        return errno ? -1 : 0;
    size_t length = strlen(entry->d_name);
    if (length >= capacity)
        return -1;
    memcpy(name, entry->d_name, length + 1);
    return 1;
}

void cc_directory_close(CcDirectory *directory) {
    if (!directory)
        return;
    closedir(directory->directory);
    free(directory);
}
#endif
