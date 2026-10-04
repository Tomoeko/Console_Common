#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1
#include "console_common/support/host.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#include "platform/windows/file_util.h"
#include <direct.h>
#include <shlobj.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

bool cc_host_time(struct timespec *value) {
    if (!value)
        return false;
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) ||
        counter.QuadPart < 0 || frequency.QuadPart <= 0)
        return false;
    value->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    value->tv_nsec = (long)((double)(counter.QuadPart % frequency.QuadPart) *
                            (1000000000.0 / (double)frequency.QuadPart));
    return true;
#else
    return clock_gettime(CLOCK_MONOTONIC, value) == 0;
#endif
}

void cc_host_sleep(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay = {.tv_sec = (time_t)(milliseconds / 1000),
                             .tv_nsec = (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}

bool cc_host_localtime(const time_t *value, struct tm *result) {
    if (!value || !result)
        return false;
#ifdef _WIN32
    return localtime_s(result, value) == 0;
#else
    return localtime_r(value, result) != NULL;
#endif
}

FILE *cc_host_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    WCHAR *wide_path = cc_windows_path(path), *wide_mode = cc_windows_wide(mode);
    FILE *file = wide_path && wide_mode ? _wfopen(wide_path, wide_mode) : NULL;
    free(wide_path);
    free(wide_mode);
    return file;
#else
    return fopen(path, mode);
#endif
}

char *cc_host_resolved_path(const char *path) {
#ifdef _WIN32
    char *resolved = cc_windows_resolved_path(path);
    if (resolved) {
        for (char *cursor = resolved; *cursor; ++cursor) {
            if (*cursor == '\\')
                *cursor = '/';
        }
    }
    return resolved;
#else
    return realpath(path, NULL);
#endif
}

bool cc_host_path_inside(const char *root, const char *path) {
    if (!root || !path)
        return false;
    char *resolved_root = cc_host_resolved_path(root);
    char *resolved = cc_host_resolved_path(path);
    if (!resolved) {
        size_t length = strlen(path);
        char *parent = malloc(length + 1);
        if (parent) {
            memcpy(parent, path, length + 1);
            char *separator = strrchr(parent, '/');
#ifdef _WIN32
            char *backslash = strrchr(parent, '\\');
            if (backslash && (!separator || backslash > separator))
                separator = backslash;
#endif
            if (separator && separator[1]) {
                *separator = '\0';
                resolved = cc_host_resolved_path(*parent ? parent : "/");
            }
            free(parent);
        }
    }
    bool inside = false;
    if (resolved_root && resolved) {
        size_t length = strlen(resolved_root);
#ifdef _WIN32
        inside = !_strnicmp(resolved_root, resolved, length) &&
                 (resolved[length] == '\\' || resolved[length] == '/' ||
                  resolved[length] == '\0');
#else
        inside = !strncmp(resolved_root, resolved, length) &&
                 (resolved[length] == '/' || resolved[length] == '\0');
#endif
    }
    free(resolved_root);
    free(resolved);
    return inside;
}

char *cc_host_executable_path(void) {
#ifdef _WIN32
    WCHAR executable[32768];
    DWORD count = GetModuleFileNameW(NULL, executable, 32768);
    char *utf8 = count && count < 32768 ? cc_windows_utf8(executable) : NULL;
    char *path = utf8 ? cc_host_resolved_path(utf8) : NULL;
    free(utf8);
#else
    char *path = NULL;
#ifdef __APPLE__
    char executable[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) == 0)
        path = realpath(executable, NULL);
#else
    char executable[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (count > 0) {
        executable[count] = '\0';
        path = realpath(executable, NULL);
    }
#endif
#endif
    return path;
}

char *cc_host_current_directory(void) {
    return cc_host_resolved_path(".");
}

static bool directory_has_marker(const char *directory, const char *marker) {
    size_t capacity = strlen(directory) + strlen(marker) + 2;
    char *candidate = malloc(capacity);
    if (!candidate)
        return false;
    snprintf(candidate, capacity, "%s/%s", directory, marker);
#ifdef _WIN32
    WCHAR *wide = cc_windows_path(candidate);
    bool found = wide && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
    free(wide);
#else
    bool found = access(candidate, F_OK) == 0;
#endif
    free(candidate);
    return found;
}

char *cc_host_project_directory(const char *marker) {
    if (!marker)
        return NULL;
    char *path = cc_host_current_directory();
    if (path && directory_has_marker(path, marker))
        return path;
    free(path);
    path = cc_host_executable_path();
    if (!path)
        return NULL;
    char *separator;
    while ((separator = strrchr(path, '/'))) {
        *separator = '\0';
        if (directory_has_marker(path, marker))
            return path;
    }
    free(path);
    return NULL;
}

bool cc_host_enter_project(const char *marker) {
    char *path = cc_host_project_directory(marker);
    if (!path)
        return false;
#ifdef _WIN32
    WCHAR *wide = cc_windows_path(path);
    bool found = wide && _wchdir(wide) == 0;
    free(wide);
#else
    bool found = chdir(path) == 0;
#endif
    free(path);
    return found;
}

char *cc_host_movies_directory(void) {
#ifdef _WIN32
    PWSTR wide = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Videos, KF_FLAG_CREATE, NULL, &wide)))
        return NULL;
    char *path = cc_windows_utf8(wide);
    CoTaskMemFree(wide);
    return path;
#else
    const char *home = getenv("HOME");
    if (!home || !*home || strlen(home) > SIZE_MAX - 8)
        return NULL;
    size_t capacity = strlen(home) + 8;
    char *path = malloc(capacity);
    if (path)
        snprintf(path, capacity, "%s/Movies", home);
    return path;
#endif
}
