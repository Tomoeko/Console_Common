#include "support/atomic_file.h"
#include "file_util.h"

#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

CcAtomicFileStatus cc_atomic_file_open(CcAtomicFile *file, const char *path) {
    if (!file || !path)
        return CC_ATOMIC_FILE_OPEN_FAILED;
    *file = (CcAtomicFile){0};
    size_t length = strlen(path);
    if (length > SIZE_MAX - 64)
        return CC_ATOMIC_FILE_PATH_TOO_LONG;
    file->temporary_path = malloc(length + 64);
    if (!file->temporary_path)
        return CC_ATOMIC_FILE_ALLOCATION_FAILED;
    HANDLE handle = INVALID_HANDLE_VALUE;
    LARGE_INTEGER tick = {0};
    QueryPerformanceCounter(&tick);
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        snprintf(file->temporary_path, length + 64, "%s.tmp.%lu.%llx.%u", path,
                 GetCurrentProcessId(), (unsigned long long)tick.QuadPart, attempt);
        handle =
            cc_windows_open_regular(file->temporary_path, GENERIC_WRITE, CREATE_NEW);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_FILE_EXISTS)
            break;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        free(file->temporary_path);
        *file = (CcAtomicFile){0};
        return CC_ATOMIC_FILE_CREATE_FAILED;
    }
    file->stream = cc_windows_stream(handle, "wb");
    if (!file->stream) {
        cc_atomic_file_discard(file);
        return CC_ATOMIC_FILE_OPEN_FAILED;
    }
    return CC_ATOMIC_FILE_OK;
}

void cc_atomic_file_discard(CcAtomicFile *file) {
    if (!file)
        return;
    if (file->stream)
        fclose(file->stream);
    WCHAR *wide = cc_windows_path(file->temporary_path);
    if (wide)
        DeleteFileW(wide);
    free(wide);
    free(file->temporary_path);
    *file = (CcAtomicFile){0};
}

bool cc_atomic_file_commit(CcAtomicFile *file, const char *path) {
    if (!file || !file->stream)
        return false;
    bool okay = !ferror(file->stream) && fflush(file->stream) == 0 &&
                _commit(_fileno(file->stream)) == 0;
    if (fclose(file->stream) != 0)
        okay = false;
    file->stream = NULL;
    WCHAR *source = cc_windows_path(file->temporary_path),
          *destination = cc_windows_path(path);
    okay = okay && source && destination &&
           MoveFileExW(source, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(source);
    free(destination);
    cc_atomic_file_discard(file);
    return okay;
}

bool cc_atomic_file_replace(const char *path, const void *data, size_t length) {
    if (length && !data)
        return false;
    CcAtomicFile file;
    if (cc_atomic_file_open(&file, path) != CC_ATOMIC_FILE_OK)
        return false;
    if (fwrite(data, 1, length, file.stream) != length) {
        cc_atomic_file_discard(&file);
        return false;
    }
    return cc_atomic_file_commit(&file, path);
}
