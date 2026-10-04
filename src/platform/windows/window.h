#ifndef CC_WINDOWS_WINDOW_H
#define CC_WINDOWS_WINDOW_H

#include "console_common/platform/platform.h"
#include <windows.h>

typedef struct CcWindowsWindow CcWindowsWindow;

/* The window owns its message thread and DC. The caller owns the GL context
 * and must release it before closing the window. No scene work runs on the
 * message thread, including inside Windows' move/size modal loop. */
CcWindowsWindow *cc_windows_window_open(const char *title, int width, int height);
HDC cc_windows_window_device(CcWindowsWindow *window);
void cc_windows_window_show(CcWindowsWindow *window);
void cc_windows_window_close(CcWindowsWindow *window);
bool cc_windows_window_poll(CcWindowsWindow *window, CcEvent *event);
bool cc_windows_window_is_fullscreen(CcWindowsWindow *window);
bool cc_windows_window_set_fullscreen(CcWindowsWindow *window, bool fullscreen);
void cc_windows_window_size(CcWindowsWindow *window, int *width, int *height);

#endif
