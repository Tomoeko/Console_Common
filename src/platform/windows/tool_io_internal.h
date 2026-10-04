#ifndef CC_WINDOWS_TOOL_IO_INTERNAL_H
#define CC_WINDOWS_TOOL_IO_INTERNAL_H

#include "file_util.h"
#include "console_common/support/tool_io.h"

int cc_tool_error(DWORD error);
char *cc_tool_child_path(int root, const char *name);
char *cc_tool_handle_path(HANDLE handle);
int cc_tool_handle_stat(HANDLE handle, struct cc_tool_stat *metadata);
bool cc_tool_private_security(SECURITY_ATTRIBUTES *attributes,
                              SECURITY_DESCRIPTOR *descriptor, PACL *acl);

#endif
