#ifndef CONSOLE_COMMON_SUPPORT_DIRECTORY_H
#define CONSOLE_COMMON_SUPPORT_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CC_PATH_ERROR,
    CC_PATH_MISSING,
    CC_PATH_FILE,
    CC_PATH_DIRECTORY,
    CC_PATH_OTHER
} CcPathKind;

typedef struct CcDirectory CcDirectory;

/* Metadata rejects links when follow_links is false. File size is optional. */
CcPathKind cc_path_information(const char *path, bool follow_links, uint64_t *size);
bool cc_directory_create(const char *path);
/* The caller owns the iterator. next returns 1, 0 at end, or -1 on error;
 * name_capacity includes the terminator. Entries are not sorted. */
CcDirectory *cc_directory_open(const char *path);
int cc_directory_next(CcDirectory *directory, char *name, size_t name_capacity);
void cc_directory_close(CcDirectory *directory);

#endif
