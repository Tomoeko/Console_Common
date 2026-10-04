#ifndef CONSOLE_COMMON_SUPPORT_PROCESS_H
#define CONSOLE_COMMON_SUPPORT_PROCESS_H

#include <stdbool.h>

/* No shell is involved. Arguments include argv[0]; NULL ends the array.
 * Return the child exit code, or -1 if launching/waiting failed. */
int cc_process_run(const char *executable, const char *working_directory,
                   char *const arguments[], bool stdout_to_stderr);

#endif
