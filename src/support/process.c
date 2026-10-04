#define _POSIX_C_SOURCE 200809L

#include "console_common/support/process.h"

#ifdef _WIN32
#include "../platform/windows/file_util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Windows parses backslashes before a quote differently from ordinary ones. */
static bool append_argument(WCHAR *line, size_t capacity, size_t *used,
                            const WCHAR *argument) {
    size_t length = wcslen(argument);
    if (length > (capacity - *used - 4) / 2)
        return false;
    if (*used)
        line[(*used)++] = L' ';
    line[(*used)++] = L'"';
    size_t slashes = 0;
    for (const WCHAR *cursor = argument;; ++cursor) {
        if (*cursor == L'\\') {
            slashes++;
            continue;
        }
        size_t count = (*cursor == L'"' || !*cursor) ? slashes * 2 : slashes;
        while (count--)
            line[(*used)++] = L'\\';
        slashes = 0;
        if (!*cursor)
            break;
        if (*cursor == L'"')
            line[(*used)++] = L'\\';
        line[(*used)++] = *cursor;
    }
    line[(*used)++] = L'"';
    line[*used] = L'\0';
    return true;
}

int cc_process_run(const char *executable, const char *working_directory,
                   char *const arguments[], bool stdout_to_stderr) {
    if (!executable || !arguments || !arguments[0])
        return -1;
    WCHAR *program = cc_windows_path(executable);
    WCHAR *directory = working_directory ? cc_windows_path(working_directory) : NULL;
    const size_t capacity = 32767;
    WCHAR *line = calloc(capacity, sizeof(*line));
    size_t used = 0;
    int result = -1;
    if (!program || (working_directory && !directory) || !line)
        goto release_strings;
    for (size_t index = 0; arguments[index]; ++index) {
        WCHAR *argument = cc_windows_wide(arguments[index]);
        bool okay = argument && used + 4 < capacity &&
                    append_argument(line, capacity, &used, argument);
        free(argument);
        if (!okay)
            goto release_strings;
    }
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdOutput =
        GetStdHandle(stdout_to_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessW(program, line, NULL, NULL, TRUE, 0, NULL, directory, &startup,
                        &process))
        goto release_strings;
    DWORD status;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &status) && status <= INT_MAX)
        result = (int)status;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
release_strings:
    free(line);
    free(directory);
    free(program);
    return result;
}
#else
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

int cc_process_run(const char *executable, const char *working_directory,
                   char *const arguments[], bool stdout_to_stderr) {
    if (!executable || !arguments || !arguments[0])
        return -1;
    pid_t child = fork();
    if (child < 0)
        return -1;
    if (child == 0) {
        if ((working_directory && chdir(working_directory) != 0) ||
            (stdout_to_stderr && dup2(STDERR_FILENO, STDOUT_FILENO) < 0))
            _exit(127);
        execv(executable, arguments);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif
