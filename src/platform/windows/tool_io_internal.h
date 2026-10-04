#ifndef CC_WINDOWS_TOOL_IO_INTERNAL_H
#define CC_WINDOWS_TOOL_IO_INTERNAL_H

#include "file_util.h"
#include "console_common/support/tool_io.h"

int cc_tool_error(DWORD error);
char *cc_tool_child_path(int root, const char *name);
char *cc_tool_handle_path(HANDLE handle);
int cc_tool_handle_stat(HANDLE handle, struct cc_tool_stat *metadata);
/* Release the owned descriptor with LocalFree after creating the object. */
bool cc_tool_private_security(SECURITY_ATTRIBUTES *attributes,
                              PSECURITY_DESCRIPTOR *descriptor);

#endif
