#include "console_common/support/directory.h"
#include "console_common/support/host.h"
#include "console_common/support/regular_file.h"
#include "support/atomic_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *root = argv[1];
    assert(cc_directory_create(root));
    assert(cc_directory_create(root));
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/file-\xCE\xA9.data", root);
    assert(length > 0 && (size_t)length < sizeof(path));
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    assert(cc_atomic_file_replace(path, payload, sizeof(payload)));
    uint64_t size;
    assert(cc_path_information(path, false, &size) == CC_PATH_FILE);
    assert(size == sizeof(payload));
    assert(cc_host_path_inside(root, path));
    char *canonical = cc_host_resolved_path(path);
    assert(canonical && cc_host_path_inside(root, canonical));
    uint8_t *bytes = NULL;
    size_t count = 0;
    assert(cc_regular_file_read_bytes(canonical, 1, 64, &bytes, &count) ==
           CC_REGULAR_FILE_OK);
    assert(count == sizeof(payload) && !memcmp(bytes, payload, count));
    free(bytes);
    free(canonical);
    assert(cc_regular_file_read_bytes(root, 1, 64, &bytes, &count) !=
           CC_REGULAR_FILE_OK);
    CcDirectory *directory = cc_directory_open(root);
    assert(directory);
    char name[1024];
    int status;
    bool found = false;
    while ((status = cc_directory_next(directory, name, sizeof(name))) > 0) {
        if (!strcmp(name, "file-\xCE\xA9.data"))
            found = true;
    }
    assert(status == 0 && found);
    cc_directory_close(directory);
    assert(cc_path_information(root, false, NULL) == CC_PATH_DIRECTORY);
    length = snprintf(path, sizeof(path), "%s/missing-file", root);
    assert(length > 0 && (size_t)length < sizeof(path));
    assert(cc_path_information(path, false, NULL) == CC_PATH_MISSING);
    return 0;
}
