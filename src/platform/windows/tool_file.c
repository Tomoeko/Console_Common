#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "tool_io_internal.h"

int cc_tool_open(const char *path, int flags, ...) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    const char *colon = strchr(path, ':');
    if (colon &&
        ((colon != path + 1 && !(strncmp(path, "//?/", 4) == 0 && colon == path + 5) &&
          !(strncmp(path, "\\\\?\\", 4) == 0 && colon == path + 5)) ||
         strchr(colon + 1, ':'))) {
        errno = EINVAL;
        return -1;
    }
    cc_tool_mode mode = 0600;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    WCHAR *wide = cc_windows_path(path);
    if (!wide) {
        errno = EINVAL;
        return -1;
    }
    DWORD access = READ_CONTROL;
    int direction = flags & (O_WRONLY | O_RDWR);
    access |= direction == O_WRONLY ? GENERIC_WRITE : GENERIC_READ;
    if (direction == O_RDWR)
        access |= GENERIC_WRITE;
    DWORD creation = (flags & O_CREAT) ? ((flags & O_EXCL) ? CREATE_NEW : OPEN_ALWAYS)
                                       : OPEN_EXISTING;
    SECURITY_ATTRIBUTES attributes = {0};
    PSECURITY_DESCRIPTOR security = NULL;
    bool private = (flags & O_CREAT) && (mode & 0077) == 0;
    if (private && !cc_tool_private_security(&attributes, &security)) {
        free(wide);
        return cc_tool_error(GetLastError());
    }
    HANDLE handle =
        CreateFileW(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    private ? &attributes : NULL, creation,
                    FILE_FLAG_BACKUP_SEMANTICS |
                        ((flags & O_NOFOLLOW) ? FILE_FLAG_OPEN_REPARSE_POINT : 0),
                    NULL);
    DWORD error = GetLastError();
    LocalFree(security);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return cc_tool_error(error);
    BY_HANDLE_FILE_INFORMATION info;
    bool valid = GetFileType(handle) == FILE_TYPE_DISK &&
                 GetFileInformationByHandle(handle, &info) &&
                 (!(flags & O_NOFOLLOW) ||
                  !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) &&
                 (((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) ==
                  ((flags & O_DIRECTORY) != 0));
    if (valid && (flags & O_TRUNC)) {
        LARGE_INTEGER beginning = {0};
        valid = SetFilePointerEx(handle, beginning, NULL, FILE_BEGIN) &&
                SetEndOfFile(handle);
    }
    if (!valid) {
        CloseHandle(handle);
        errno = EINVAL;
        return -1;
    }
    int file = _open_osfhandle((intptr_t)handle, direction | _O_BINARY | _O_NOINHERIT);
    if (file < 0)
        CloseHandle(handle);
    return file;
}

int cc_tool_openat(int root, const char *name, int flags, ...) {
    cc_tool_mode mode = 0600;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    char *path = cc_tool_child_path(root, name);
    int file = path ? cc_tool_open(path, flags, mode) : -1;
    free(path);
    return file;
}

ssize_t cc_tool_read(int file, void *bytes, size_t size) {
    unsigned amount = size > INT_MAX ? INT_MAX : (unsigned)size;
    return _read(file, bytes, amount);
}

ssize_t cc_tool_write(int file, const void *bytes, size_t size) {
    unsigned amount = size > INT_MAX ? INT_MAX : (unsigned)size;
    return _write(file, bytes, amount);
}

int cc_tool_fsync(int file) {
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE handle = (HANDLE)_get_osfhandle(file);
    if (!GetFileInformationByHandle(handle, &info))
        return cc_tool_error(GetLastError());
    /* Windows has no directory fsync equivalent. Publishing uses
     * MoveFileEx(MOVEFILE_WRITE_THROUGH); file data is flushed separately. */
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return 0;
    return _commit(file);
}

int cc_tool_lock(int file, bool wait) {
    HANDLE handle = (HANDLE)_get_osfhandle(file);
    OVERLAPPED position = {0};
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
    return LockFileEx(handle, flags, 0, MAXDWORD, MAXDWORD, &position)
               ? 0
               : cc_tool_error(GetLastError());
}

int cc_tool_flock(int file, int flags) {
    if (!(flags & LOCK_EX)) {
        errno = EINVAL;
        return -1;
    }
    return cc_tool_lock(file, !(flags & LOCK_NB));
}
