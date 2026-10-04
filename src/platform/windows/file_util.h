#ifndef CC_WINDOWS_FILE_UTIL_H
#define CC_WINDOWS_FILE_UTIL_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

/* UTF-8 is the shared path encoding; returned strings belong to the caller. */
WCHAR *cc_windows_wide(const char *text);
WCHAR *cc_windows_path(const char *path);
char *cc_windows_utf8(const WCHAR *text);
HANDLE cc_windows_open_regular(const char *path, DWORD access, DWORD creation);
HANDLE cc_windows_open_directory(const char *path);
FILE *cc_windows_stream(HANDLE handle, const char *mode);
char *cc_windows_resolved_path(const char *path);
bool cc_windows_create_directory(const char *path);
bool cc_windows_path_missing(const char *path);

#endif
