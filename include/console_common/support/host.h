#ifndef CONSOLE_COMMON_SUPPORT_HOST_H
#define CONSOLE_COMMON_SUPPORT_HOST_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

/* Host services do not advance a console clock or interpret scene state. */
bool cc_host_time(struct timespec *value);
void cc_host_sleep(unsigned milliseconds);
bool cc_host_localtime(const time_t *value, struct tm *result);
/* Owned canonical path; separators are normalized to slash on Windows. */
char *cc_host_resolved_path(const char *path);
char *cc_host_executable_path(void);
char *cc_host_current_directory(void);
FILE *cc_host_fopen(const char *path, const char *mode);
/* Search executable parents only when the current directory lacks marker. */
char *cc_host_project_directory(const char *marker);
bool cc_host_enter_project(const char *marker);
/* Resolve existing objects (or a new leaf's parent), including native links. */
bool cc_host_path_inside(const char *root, const char *path);
/* The caller owns the native user's Movies/Videos directory string. */
char *cc_host_movies_directory(void);

#endif
