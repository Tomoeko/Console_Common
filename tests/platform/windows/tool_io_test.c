#include <windows.h>

#include "console_common/support/tool_io.h"
#include "console_common/support/host.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include "junction.h"

static void expect_file(const char *path, const char *text) {
    FILE *stream = cc_host_fopen(path, "rb");
    char bytes[16] = {0};
    assert(stream && fread(bytes, 1, sizeof(bytes) - 1, stream) == strlen(text));
    assert(fclose(stream) == 0 && !strcmp(bytes, text));
}

int main(void) {
    char root[] = "native tools caf\xc3\xa9-XXXXXX";
    assert(mkdtemp(root));
    struct stat metadata;
    assert(lstat(root, &metadata) == 0 && S_ISDIR(metadata.st_mode));
    assert(metadata.st_uid == getuid() && (metadata.st_mode & 0777) == 0700);
    int directory = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(directory >= 0 && fsync(directory) == 0);
    assert(mkdirat(directory, "child", 0700) == 0);
    int child = openat(directory, "child", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(child >= 0);
    int file = openat(child, "probe.bin", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    assert(file >= 0 && write(file, "keep", 4) == 4 && fsync(file) == 0);
    assert(fstat(file, &metadata) == 0 && metadata.st_size == 4);
    assert(metadata.st_uid == getuid() && !(metadata.st_mode & 0022));
    assert(close(file) == 0);
    assert(openat(child, "probe.bin", O_WRONLY | O_CREAT | O_EXCL, 0600) < 0);
    assert(openat(child, "../escape", O_WRONLY | O_CREAT, 0600) < 0);
    assert(openat(child, "stream:extra", O_WRONLY | O_CREAT, 0600) < 0);
    assert(fstatat(child, "probe.bin", &metadata, AT_SYMLINK_NOFOLLOW) == 0);
    DIR *listing = fdopendir(dup(child));
    assert(listing);
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(listing)))
        found |= !strcmp(entry->d_name, "probe.bin");
    assert(found && closedir(listing) == 0);
    char path[4096], second_path[4096];
    snprintf(path, sizeof(path), "%s/child/probe.bin", root);
    expect_file(path, "keep");
    char *canonical = realpath(path, NULL);
    assert(canonical);
    expect_file(canonical, "keep");
    free(canonical);
    snprintf(second_path, sizeof(second_path), "%s/child/second.bin", root);
    int second = open(second_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(second >= 0 && write(second, "new", 3) == 3 && close(second) == 0);
    assert(!cc_tool_publish(second_path, path));
    expect_file(path, "keep");
    assert(rename(second_path, path) == 0);
    expect_file(path, "new");
    char link[4096];
    snprintf(link, sizeof(link), "%s/linked", root);
    snprintf(second_path, sizeof(second_path), "%s/child", root);
    assert(cc_test_junction(link, second_path));
    assert(open(link, O_RDONLY | O_DIRECTORY | O_NOFOLLOW) < 0);
    assert(lstat(link, &metadata) == 0 && !S_ISDIR(metadata.st_mode));
    assert(rmdir(link) == 0);
    int lock = open(path, O_RDWR | O_NOFOLLOW);
    int contender = open(path, O_RDWR | O_NOFOLLOW);
    assert(lock >= 0 && contender >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    assert(flock(contender, LOCK_EX | LOCK_NB) < 0);
    assert(close(lock) == 0 && flock(contender, LOCK_EX | LOCK_NB) == 0);
    assert(close(contender) == 0);
    assert(unlinkat(child, "probe.bin", 0) == 0 && close(child) == 0);
    snprintf(path, sizeof(path), "%s/child", root);
    assert(rmdir(path) == 0 && close(directory) == 0 && rmdir(root) == 0);
    return 0;
}
