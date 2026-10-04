#ifndef CONSOLE_COMMON_SUPPORT_TOOL_IO_H
#define CONSOLE_COMMON_SUPPORT_TOOL_IO_H

/* Opt-in file operations for the extraction/preparation tools. The Windows
 * adapter preserves exclusive creation, no-follow, ownership and lock checks;
 * this header is not used by renderer or audio code. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

bool cc_tool_random(void *bytes, size_t size);

#ifdef _WIN32
#include <io.h>

typedef int cc_tool_mode;
typedef int64_t cc_tool_offset;
typedef ptrdiff_t ssize_t;
struct cc_tool_stat {
    int64_t st_size;
    unsigned st_mode;
    unsigned st_uid;
};
typedef struct CcToolDirectory DIR;
struct dirent {
    char d_name[1024];
};

#define mode_t cc_tool_mode
#define off_t cc_tool_offset
#define O_NOFOLLOW 0x100000
#define O_DIRECTORY 0x200000
#define O_CLOEXEC 0x400000
#define O_NONBLOCK 0x800000
#define AT_SYMLINK_NOFOLLOW 1
#define LOCK_EX 1
#define LOCK_NB 2
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)

int cc_tool_open(const char *path, int flags, ...);
int cc_tool_openat(int root, const char *name, int flags, ...);
int cc_tool_mkdir(const char *path, cc_tool_mode mode);
int cc_tool_mkdirat(int root, const char *name, cc_tool_mode mode);
int cc_tool_stat(const char *path, struct cc_tool_stat *metadata);
int cc_tool_lstat(const char *path, struct cc_tool_stat *metadata);
int cc_tool_fstat(int file, struct cc_tool_stat *metadata);
int cc_tool_fstatat(int root, const char *name, struct cc_tool_stat *metadata,
                    int flags);
int cc_tool_unlink(const char *path);
int cc_tool_unlinkat(int root, const char *name, int flags);
int cc_tool_rmdir(const char *path);
int cc_tool_rename(const char *source, const char *destination);
bool cc_tool_publish(const char *source, const char *destination);
int cc_tool_fsync(int file);
int cc_tool_flock(int file, int flags);
int cc_tool_lock(int file, bool wait);
ssize_t cc_tool_read(int file, void *bytes, size_t size);
ssize_t cc_tool_write(int file, const void *bytes, size_t size);
char *cc_tool_realpath(const char *path, char *result);
char *cc_tool_mkdtemp(char *path);
int cc_tool_mkstemp(char *path);
DIR *cc_tool_opendir(const char *path);
DIR *cc_tool_fdopendir(int file);
struct dirent *cc_tool_readdir(DIR *directory);
int cc_tool_closedir(DIR *directory);
int cc_tool_scandir(const char *path, struct dirent ***entries,
                    int (*filter)(const struct dirent *),
                    int (*compare)(const struct dirent **, const struct dirent **));
int cc_tool_alphasort(const struct dirent **left, const struct dirent **right);
size_t cc_tool_root_length(const char *path);

#define open cc_tool_open
#define openat cc_tool_openat
#define mkdir cc_tool_mkdir
#define mkdirat cc_tool_mkdirat
#define stat cc_tool_stat
#define lstat cc_tool_lstat
#define fstat cc_tool_fstat
#define fstatat cc_tool_fstatat
#define unlink cc_tool_unlink
#define unlinkat cc_tool_unlinkat
#define rmdir cc_tool_rmdir
#define rename cc_tool_rename
#define fsync cc_tool_fsync
#define flock cc_tool_flock
#define read cc_tool_read
#define write cc_tool_write
#define realpath cc_tool_realpath
#define mkdtemp cc_tool_mkdtemp
#define mkstemp cc_tool_mkstemp
#define opendir cc_tool_opendir
#define fdopendir cc_tool_fdopendir
#define readdir cc_tool_readdir
#define closedir cc_tool_closedir
#define scandir cc_tool_scandir
#define alphasort cc_tool_alphasort
#define close _close
#define dup _dup
#define fdopen _fdopen
#define fileno _fileno
#define fseeko _fseeki64
#define ftello _ftelli64
#define strdup _strdup
/* Metadata reports current-owner status from the native SID, not account text. */
#define getuid() 1U
#else
#include <dirent.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#endif
