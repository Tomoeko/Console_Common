#include <windows.h>
#include <aclapi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tool_io_internal.h"

int cc_tool_error(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            errno = EEXIST;
            break;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            errno = EBUSY;
            break;
        case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
            break;
        case ERROR_DIRECTORY:
            errno = ENOTDIR;
            break;
        default:
            errno = EIO;
            break;
    }
    return -1;
}

static TOKEN_USER *current_user(void) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return NULL;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &size);
    TOKEN_USER *user = size ? malloc(size) : NULL;
    if (user && !GetTokenInformation(token, TokenUser, user, size, &size)) {
        free(user);
        user = NULL;
    }
    CloseHandle(token);
    return user;
}

bool cc_tool_private_security(SECURITY_ATTRIBUTES *attributes,
                              SECURITY_DESCRIPTOR *descriptor, PACL *acl) {
    TOKEN_USER *user = current_user();
    if (!user)
        return false;
    EXPLICIT_ACCESSW access = {0};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = user->User.Sid;
    bool okay =
        SetEntriesInAclW(1, &access, NULL, acl) == ERROR_SUCCESS &&
        InitializeSecurityDescriptor(descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(descriptor, TRUE, *acl, FALSE);
    free(user);
    if (okay) {
        attributes->nLength = sizeof(*attributes);
        attributes->lpSecurityDescriptor = descriptor;
        attributes->bInheritHandle = FALSE;
    }
    return okay;
}

static bool acl_private(PACL acl, PSID owner) {
    if (!acl)
        return false;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *entry;
        if (!GetAce(acl, index, &entry))
            return false;
        ACE_HEADER *header = entry;
        if (header->AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            if (header->AceType != ACCESS_DENIED_ACE_TYPE)
                return false;
            continue;
        }
        ACCESS_ALLOWED_ACE *allowed = entry;
        DWORD writes = GENERIC_ALL | GENERIC_WRITE | DELETE | WRITE_DAC | WRITE_OWNER |
                       FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD;
        PSID sid = &allowed->SidStart;
        if ((allowed->Mask & writes) && !EqualSid(sid, owner) &&
            !IsWellKnownSid(sid, WinLocalSystemSid) &&
            !IsWellKnownSid(sid, WinBuiltinAdministratorsSid))
            return false;
    }
    return true;
}

int cc_tool_handle_stat(HANDLE handle, struct cc_tool_stat *metadata) {
    BY_HANDLE_FILE_INFORMATION info;
    if (!metadata || !GetFileInformationByHandle(handle, &info))
        return cc_tool_error(GetLastError());
    memset(metadata, 0, sizeof(*metadata));
    metadata->st_size =
        (int64_t)(((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return 0;
    metadata->st_mode =
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR : _S_IFREG;
    TOKEN_USER *user = current_user();
    PSECURITY_DESCRIPTOR security = NULL;
    PSID owner = NULL;
    PACL acl = NULL;
    bool owned =
        user &&
        GetSecurityInfo(handle, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                        NULL, &acl, NULL, &security) == ERROR_SUCCESS &&
        owner && EqualSid(owner, user->User.Sid);
    metadata->st_uid = owned ? 1U : 0U;
    /* Recovery checks need ownership and private-write status, not emulated
     * Unix access bits. Native ACLs determine these two fields. */
    metadata->st_mode |= owned && acl_private(acl, owner) ? 0700U : 0777U;
    LocalFree(security);
    free(user);
    return 0;
}

static int path_stat(const char *path, struct cc_tool_stat *metadata, bool follow) {
    WCHAR *wide = cc_windows_path(path);
    if (!wide) {
        errno = EINVAL;
        return -1;
    }
    HANDLE file = CreateFileW(
        wide, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
    free(wide);
    if (file == INVALID_HANDLE_VALUE)
        return cc_tool_error(GetLastError());
    int result = cc_tool_handle_stat(file, metadata);
    CloseHandle(file);
    return result;
}

int cc_tool_stat(const char *path, struct cc_tool_stat *metadata) {
    return path_stat(path, metadata, true);
}

int cc_tool_lstat(const char *path, struct cc_tool_stat *metadata) {
    return path_stat(path, metadata, false);
}

int cc_tool_fstat(int file, struct cc_tool_stat *metadata) {
    return cc_tool_handle_stat((HANDLE)_get_osfhandle(file), metadata);
}

int cc_tool_fstatat(int root, const char *name, struct cc_tool_stat *metadata,
                    int flags) {
    char *path = cc_tool_child_path(root, name);
    int result = path ? path_stat(path, metadata, !(flags & AT_SYMLINK_NOFOLLOW)) : -1;
    free(path);
    return result;
}
