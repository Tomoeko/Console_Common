#include "../gles2/host.h"
#include "gl_api.h"
#include "window.h"

#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CcGles2Host {
    CcWindowsWindow *window;
    HDC device;
    HGLRC context;
    bool timer_period_active;
    bool swap_failure_reported;
};

CcGles2Host *cc_gles2_host_create(const char *title, int width, int height) {
    CcGles2Host *host = calloc(1, sizeof(*host));
    if (!host)
        return NULL;
    host->timer_period_active = timeBeginPeriod(1) == TIMERR_NOERROR;
    host->window = cc_windows_window_open(title, width, height);
    if (!host->window)
        goto release_host;
    host->device = cc_windows_window_device(host->window);
    PIXELFORMATDESCRIPTOR format = {.nSize = sizeof(format),
                                    .nVersion = 1,
                                    .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                                               PFD_DOUBLEBUFFER,
                                    .iPixelType = PFD_TYPE_RGBA,
                                    .cColorBits = 32,
                                    .cAlphaBits = 8,
                                    .cDepthBits = 16,
                                    .iLayerType = PFD_MAIN_PLANE};
    int pixel_format = ChoosePixelFormat(host->device, &format);
    if (!pixel_format || !SetPixelFormat(host->device, pixel_format, &format))
        goto release_host;
    host->context = wglCreateContext(host->device);
    if (!host->context || !cc_gles2_host_make_current(host) || !cc_windows_gl_load())
        goto release_host;
    typedef BOOL(WINAPI * SwapInterval)(int);
    PROC address = wglGetProcAddress("wglSwapIntervalEXT");
    SwapInterval swap_interval = NULL;
    if (address && (uintptr_t)address > 3 && (uintptr_t)address != UINTPTR_MAX) {
        memcpy(&swap_interval, &address, sizeof(swap_interval));
        /* The application loop already paces console frames. A second wait at
         * swap can halve presentation rate when a sleep misses vblank. */
        swap_interval(0);
    }
    return host;
release_host:
    fprintf(stderr, "Windows: graphics initialization failed (%lu).\n", GetLastError());
    cc_gles2_host_destroy(host);
    return NULL;
}

void cc_gles2_host_show(CcGles2Host *host) {
    cc_windows_window_show(host->window);
}

bool cc_gles2_host_make_current(CcGles2Host *host) {
    return host && wglMakeCurrent(host->device, host->context) != 0;
}

bool cc_gles2_host_preserve_back_buffer(CcGles2Host *host) {
    (void)host;
    return false;
}

void cc_gles2_host_destroy(CcGles2Host *host) {
    if (!host)
        return;
    if (host->context) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(host->context);
    }
    cc_windows_window_close(host->window);
    if (host->timer_period_active)
        timeEndPeriod(1);
    free(host);
}

bool cc_gles2_host_poll(CcGles2Host *host, CcEvent *event) {
    return host && event && cc_windows_window_poll(host->window, event);
}

bool cc_gles2_host_is_fullscreen(CcGles2Host *host) {
    return host && cc_windows_window_is_fullscreen(host->window);
}

bool cc_gles2_host_set_fullscreen(CcGles2Host *host, bool fullscreen) {
    return host && cc_windows_window_set_fullscreen(host->window, fullscreen);
}

void cc_gles2_host_surface_size(CcGles2Host *host, int *width, int *height) {
    cc_windows_window_size(host->window, width, height);
}

bool cc_gles2_host_present(CcGles2Host *host) {
    if (SwapBuffers(host->device))
        return true;
    if (!host->swap_failure_reported) {
        fprintf(stderr, "Windows: presentation failed (%lu).\n", GetLastError());
        host->swap_failure_reported = true;
    }
    return false;
}
