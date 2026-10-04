#include "gl_api.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

CcWindowsGl cc_windows_gl;

static PROC entry_point(const char *name) {
    PROC address = wglGetProcAddress(name);
    uintptr_t value = (uintptr_t)address;
    if (value == 0 || value <= 3 || value == UINTPTR_MAX)
        return NULL;
    return address;
}

bool cc_windows_gl_load(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    int major = 0, minor = 0;
    if (!version || sscanf(version, "%d.%d", &major, &minor) != 2 || major < 2 ||
        (major == 2 && minor < 1)) {
        fprintf(stderr, "Windows: an OpenGL 2.1 graphics driver is required.\n");
        return false;
    }
    CcWindowsGl functions = {0};
#define CC_GL_LOAD(result, name, arguments)                                            \
    {                                                                                  \
        PROC address = entry_point("gl" #name);                                        \
        if (!address)                                                                  \
            address = entry_point("gl" #name "EXT");                                   \
        if (!address) {                                                                \
            fprintf(stderr, "Windows: missing OpenGL function gl%s.\n", #name);        \
            return false;                                                              \
        }                                                                              \
        _Static_assert(sizeof(functions.name) == sizeof(address), "GL pointer size");  \
        memcpy(&functions.name, &address, sizeof(address));                            \
    }
    CC_GL_FUNCTIONS(CC_GL_LOAD)
#undef CC_GL_LOAD
    cc_windows_gl = functions;
    return true;
}
