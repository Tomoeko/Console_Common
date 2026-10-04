#ifndef CC_TEST_WINDOWS_JUNCTION_H
#define CC_TEST_WINDOWS_JUNCTION_H

#include <windows.h>
#include <winioctl.h>
#include "platform/windows/file_util.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Directory junctions do not require Developer Mode or symlink privilege. */
static bool cc_test_junction(const char *link, const char *target) {
    char *canonical = cc_windows_resolved_path(target);
    WCHAR *destination = canonical ? cc_windows_path(canonical) : NULL;
    WCHAR *name = cc_windows_path(link);
    free(canonical);
    bool okay = false;
    HANDLE handle = INVALID_HANDLE_VALUE;
    struct MountPoint {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
        WCHAR paths[4096];
    } *point = calloc(1, sizeof(*point));
    if (!destination || !name || !point || wcsncmp(destination, L"\\\\?\\", 4))
        goto release_paths;
    size_t length = wcslen(destination);
    if (length > 2000)
        goto release_paths;
    destination[1] = L'?';
    point->tag = IO_REPARSE_TAG_MOUNT_POINT;
    point->substitute_length = (WORD)(length * sizeof(WCHAR));
    point->print_offset = (WORD)((length + 1) * sizeof(WCHAR));
    point->print_length = (WORD)((length - 4) * sizeof(WCHAR));
    memcpy(point->paths, destination, (length + 1) * sizeof(WCHAR));
    memcpy(point->paths + length + 1, destination + 4, (length - 3) * sizeof(WCHAR));
    point->data_length = (WORD)(8 + (length * 2 - 2) * sizeof(WCHAR));
    if (!CreateDirectoryW(name, NULL))
        goto release_paths;
    handle =
        CreateFileW(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD written;
    okay = handle != INVALID_HANDLE_VALUE &&
           DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, point,
                           point->data_length + 8U, NULL, 0, &written, NULL);
release_paths:
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (!okay && name)
        RemoveDirectoryW(name);
    free(point);
    free(name);
    free(destination);
    return okay;
}

#endif
