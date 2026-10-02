#ifndef CONSOLE_COMMON_ATOMIC_FILE_H
#define CONSOLE_COMMON_ATOMIC_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum CcAtomicFileStatus {
    CC_ATOMIC_FILE_OK,
    CC_ATOMIC_FILE_PATH_TOO_LONG,
    CC_ATOMIC_FILE_ALLOCATION_FAILED,
    CC_ATOMIC_FILE_CREATE_FAILED,
    CC_ATOMIC_FILE_OPEN_FAILED
} CcAtomicFileStatus;

/* Owns a same-directory temporary file until commit or discard consumes it. */
typedef struct CcAtomicFile {
    char *temporary_path;
    FILE *stream;
} CcAtomicFile;

CcAtomicFileStatus cc_atomic_file_open(CcAtomicFile *file, const char *path);
/* Flush and sync file contents, then atomically replace the destination. A
 * true result means rename succeeded; containing-directory persistence after
 * power loss is not promised by this interface. Commit consumes the file. */
bool cc_atomic_file_commit(CcAtomicFile *file, const char *path);
void cc_atomic_file_discard(CcAtomicFile *file);
bool cc_atomic_file_replace(const char *path, const void *data, size_t length);

#endif
