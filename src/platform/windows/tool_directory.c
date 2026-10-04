#include <windows.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "tool_io_internal.h"
#include "console_common/support/directory.h"

struct CcToolDirectory {
    CcDirectory *iterator;
    struct dirent entry;
    int descriptor;
};

DIR *cc_tool_fdopendir(int file) {
    struct cc_tool_stat metadata;
    if (cc_tool_fstat(file, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    char *path = cc_tool_handle_path((HANDLE)_get_osfhandle(file));
    DIR *directory = path ? calloc(1, sizeof(*directory)) : NULL;
    if (directory) {
        directory->iterator = cc_directory_open(path);
        directory->descriptor = file;
        if (!directory->iterator) {
            free(directory);
            directory = NULL;
        }
    }
    free(path);
    return directory;
}

DIR *cc_tool_opendir(const char *path) {
    int file = cc_tool_open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (file < 0)
        return NULL;
    DIR *directory = cc_tool_fdopendir(file);
    if (!directory)
        _close(file);
    return directory;
}

struct dirent *cc_tool_readdir(DIR *directory) {
    int result = cc_directory_next(directory->iterator, directory->entry.d_name,
                                   sizeof(directory->entry.d_name));
    if (result < 0)
        errno = EIO;
    return result > 0 ? &directory->entry : NULL;
}

int cc_tool_closedir(DIR *directory) {
    cc_directory_close(directory->iterator);
    int result = _close(directory->descriptor);
    free(directory);
    return result;
}

int cc_tool_alphasort(const struct dirent **left, const struct dirent **right) {
    return strcmp((*left)->d_name, (*right)->d_name);
}

typedef struct {
    int (*compare)(const struct dirent **, const struct dirent **);
} DirectorySort;

static int compare_entries(void *context, const void *left, const void *right) {
    DirectorySort *sort = context;
    const struct dirent *left_entry = *(struct dirent *const *)left;
    const struct dirent *right_entry = *(struct dirent *const *)right;
    return sort->compare(&left_entry, &right_entry);
}

int cc_tool_scandir(const char *path, struct dirent ***entries,
                    int (*filter)(const struct dirent *),
                    int (*compare)(const struct dirent **, const struct dirent **)) {
    DIR *directory = cc_tool_opendir(path);
    if (!directory)
        return -1;
    struct dirent **list = NULL;
    size_t count = 0, capacity = 0;
    bool okay = true;
    struct dirent *entry;
    errno = 0;
    while ((entry = cc_tool_readdir(directory)) != NULL) {
        if (filter && !filter(entry))
            continue;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 32;
            if (next > INT_MAX || next > SIZE_MAX / sizeof(*list)) {
                okay = false;
                break;
            }
            struct dirent **larger = realloc(list, next * sizeof(*list));
            if (!larger) {
                okay = false;
                break;
            }
            list = larger;
            capacity = next;
        }
        list[count] = malloc(sizeof(*entry));
        if (!list[count]) {
            okay = false;
            break;
        }
        *list[count++] = *entry;
    }
    bool read_error = errno != 0;
    if (cc_tool_closedir(directory) != 0 || read_error)
        okay = false;
    if (!okay) {
        for (size_t index = 0; index < count; ++index)
            free(list[index]);
        free(list);
        return -1;
    }
    if (compare) {
        DirectorySort sort = {compare};
        qsort_s(list, count, sizeof(*list), compare_entries, &sort);
    }
    *entries = list;
    return (int)count;
}
