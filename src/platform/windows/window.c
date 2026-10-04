#include "window.h"
#include "console_common/render/viewport.h"

#include <windowsx.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CC_WINDOW_EVENTS = 64,
    CC_WINDOW_SHOW = WM_APP + 1,
    CC_WINDOW_STOP,
    CC_WINDOW_FULLSCREEN
};

struct CcWindowsWindow {
    HWND window;
    HDC device;
    HANDLE thread;
    HANDLE ready;
    CRITICAL_SECTION mutex;
    WCHAR title[256];
    int width;
    int height;
    bool opened;
    bool fullscreen;
    bool tracking_pointer;
    bool quit;
    bool resized;
    bool lost_events;
    WINDOWPLACEMENT placement;
    DWORD window_style;
    CcEvent events[CC_WINDOW_EVENTS];
    unsigned read;
    unsigned count;
    CcKey held_keys[256];
};

static bool change_fullscreen(CcWindowsWindow *host, bool fullscreen);

static void queue_event(CcWindowsWindow *host, CcEvent event) {
    EnterCriticalSection(&host->mutex);
    if (host->count == CC_WINDOW_EVENTS) {
        host->lost_events = true;
        host->read = host->count = 0;
        memset(host->held_keys, 0, sizeof(host->held_keys));
    } else if (event.type == CC_EVENT_POINTER_MOVE && host->count &&
               host->events[(host->read + host->count - 1) % CC_WINDOW_EVENTS].type ==
                   CC_EVENT_POINTER_MOVE) {
        host->events[(host->read + host->count - 1) % CC_WINDOW_EVENTS] = event;
    } else {
        host->events[(host->read + host->count) % CC_WINDOW_EVENTS] = event;
        ++host->count;
    }
    LeaveCriticalSection(&host->mutex);
}

static CcKey key_code(WPARAM key) {
    switch (key) {
        case VK_LEFT:
            return CC_KEY_LEFT;
        case VK_RIGHT:
            return CC_KEY_RIGHT;
        case VK_UP:
            return CC_KEY_UP;
        case VK_DOWN:
            return CC_KEY_DOWN;
        case VK_RETURN:
            return CC_KEY_ENTER;
        case VK_ESCAPE:
            return CC_KEY_ESCAPE;
        case VK_BACK:
            return CC_KEY_BACKSPACE;
        case VK_HOME:
            return CC_KEY_HOME;
        case VK_SHIFT:
            return CC_KEY_SHIFT;
        case VK_CAPITAL:
            return CC_KEY_CAPS_LOCK;
        default:
            break;
    }
    BYTE state[256];
    WCHAR text[4];
    if (GetKeyboardState(state) &&
        ToUnicode((UINT)key, MapVirtualKeyW((UINT)key, MAPVK_VK_TO_VSC), state, text, 4,
                  0) == 1 &&
        text[0] >= 32 && text[0] < 127)
        return (CcKey)text[0];
    return CC_KEY_UNKNOWN;
}

static CcEvent pointer_event(CcWindowsWindow *host, CcEventType type, LPARAM position) {
    CcEvent event = {.type = type};
    CcViewport viewport = cc_viewport_fit(host->width, host->height);
    int x = GET_X_LPARAM(position), y = GET_Y_LPARAM(position);
    event.outside_viewport =
        !cc_viewport_map_pointer_unbounded(viewport, x, y, &event.x, &event.y);
    return event;
}

static LRESULT CALLBACK window_message(HWND window, UINT message, WPARAM first,
                                       LPARAM second) {
    CcWindowsWindow *host = (CcWindowsWindow *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        host = ((CREATESTRUCTW *)second)->lpCreateParams;
        host->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)host);
    }
    if (!host)
        return DefWindowProcW(window, message, first, second);
    switch (message) {
        case CC_WINDOW_SHOW:
            ShowWindow(window, SW_SHOW);
            UpdateWindow(window);
            return 0;
        case CC_WINDOW_STOP:
            if (host->device) {
                ReleaseDC(window, host->device);
                host->device = NULL;
            }
            DestroyWindow(window);
            return 0;
        case CC_WINDOW_FULLSCREEN:
            return change_fullscreen(host, first != 0);
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED: {
            const RECT *bounds = (const RECT *)second;
            if (!host->fullscreen)
                SetWindowPos(window, NULL, bounds->left, bounds->top,
                             bounds->right - bounds->left, bounds->bottom - bounds->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            EnterCriticalSection(&host->mutex);
            host->resized = true;
            LeaveCriticalSection(&host->mutex);
            return 0;
        }
        case WM_CLOSE:
            EnterCriticalSection(&host->mutex);
            host->quit = true;
            LeaveCriticalSection(&host->mutex);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            if (LOWORD(second) == HTCLIENT) {
                SetCursor(NULL);
                return TRUE;
            }
            return DefWindowProcW(window, message, first, second);
        case WM_SIZE:
            EnterCriticalSection(&host->mutex);
            host->width = LOWORD(second);
            host->height = HIWORD(second);
            host->resized = true;
            LeaveCriticalSection(&host->mutex);
            return 0;
        case WM_KILLFOCUS:
            memset(host->held_keys, 0, sizeof(host->held_keys));
            queue_event(host, (CcEvent){.type = CC_EVENT_POINTER_LEAVE,
                                        .cancel_capture = true});
            ReleaseCapture();
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP: {
            if (first >= 256)
                return 0;
            bool down = message == WM_KEYDOWN;
            CcKey key = host->held_keys[first];
            if (down && key == CC_KEY_UNKNOWN)
                key = key_code(first);
            host->held_keys[first] = down ? key : CC_KEY_UNKNOWN;
            CcEvent event = {.type = down ? CC_EVENT_KEY_DOWN : CC_EVENT_KEY_UP,
                             .key = key,
                             .key_repeat = down && (second & (1L << 30)) != 0,
                             .shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                             .caps_lock_on = (GetKeyState(VK_CAPITAL) & 1) != 0};
            if (key == CC_KEY_SHIFT || key == CC_KEY_CAPS_LOCK)
                event.type = CC_EVENT_KEY_MODIFIERS;
            queue_event(host, event);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!host->tracking_pointer) {
                TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
                host->tracking_pointer = TrackMouseEvent(&tracking) != 0;
            }
            queue_event(host, pointer_event(host, CC_EVENT_POINTER_MOVE, second));
            return 0;
        }
        case WM_MOUSELEAVE:
            host->tracking_pointer = false;
            queue_event(host, (CcEvent){.type = CC_EVENT_POINTER_LEAVE});
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                        message == WM_MBUTTONDOWN;
            CcEvent event = pointer_event(
                host, down ? CC_EVENT_POINTER_DOWN : CC_EVENT_POINTER_UP, second);
            event.button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP
                               ? CC_POINTER_LEFT
                           : message == WM_RBUTTONDOWN || message == WM_RBUTTONUP
                               ? CC_POINTER_RIGHT
                               : CC_POINTER_MIDDLE;
            if (down)
                SetCapture(window);
            else
                ReleaseCapture();
            queue_event(host, event);
            return 0;
        }
        default:
            return DefWindowProcW(window, message, first, second);
    }
}

static void enable_dpi_scaling(void) {
    typedef BOOL(WINAPI * DpiAwareness)(HANDLE);
    PROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                  "SetProcessDpiAwarenessContext");
    DpiAwareness awareness = NULL;
    if (address)
        memcpy(&awareness, &address, sizeof(awareness));
    if (awareness)
        awareness((HANDLE)(INT_PTR)-4);
    else
        SetProcessDPIAware();
}

static bool create_window(CcWindowsWindow *host) {
    enable_dpi_scaling();
    HINSTANCE instance = GetModuleHandleW(NULL);
    static const WCHAR class_name[] = L"ConsoleCommonOpenGLWindow";
    WNDCLASSW window_class = {.style = CS_OWNDC,
                              .lpfnWndProc = window_message,
                              .hInstance = instance,
                              .hCursor = LoadCursorW(NULL, IDC_ARROW),
                              .lpszClassName = class_name};
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    RECT bounds = {0, 0, host->width, host->height};
    host->window_style = WS_OVERLAPPEDWINDOW;
    if (!AdjustWindowRect(&bounds, host->window_style, FALSE))
        return false;
    host->window =
        CreateWindowW(class_name, host->title, host->window_style, CW_USEDEFAULT,
                      CW_USEDEFAULT, bounds.right - bounds.left,
                      bounds.bottom - bounds.top, NULL, NULL, instance, host);
    if (!host->window)
        return false;
    host->device = GetDC(host->window);
    return host->device != NULL;
}

static unsigned __stdcall window_thread(void *context) {
    CcWindowsWindow *host = context;
    host->opened = create_window(host);
    if (!host->opened)
        fprintf(stderr, "Windows: window initialization failed (%lu).\n",
                GetLastError());
    SetEvent(host->ready);
    if (host->opened) {
        MSG message;
        int status;
        while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        EnterCriticalSection(&host->mutex);
        host->quit = true;
        LeaveCriticalSection(&host->mutex);
    }
    if (host->device)
        ReleaseDC(host->window, host->device);
    if (IsWindow(host->window))
        DestroyWindow(host->window);
    return 0;
}

CcWindowsWindow *cc_windows_window_open(const char *title, int width, int height) {
    if (!title || width <= 0 || height <= 0)
        return NULL;
    CcWindowsWindow *host = calloc(1, sizeof(*host));
    if (!host)
        return NULL;
    if (!InitializeCriticalSectionEx(&host->mutex, 0, 0)) {
        free(host);
        return NULL;
    }
    host->width = width;
    host->height = height;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title, -1, host->title,
                             256))
        goto release_host;
    host->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!host->ready)
        goto release_host;
    host->thread = (HANDLE)_beginthreadex(NULL, 0, window_thread, host, 0, NULL);
    if (!host->thread)
        goto release_host;
    if (WaitForSingleObject(host->ready, INFINITE) != WAIT_OBJECT_0 || !host->opened)
        goto release_host;
    CloseHandle(host->ready);
    host->ready = NULL;
    return host;
release_host:
    cc_windows_window_close(host);
    return NULL;
}

HDC cc_windows_window_device(CcWindowsWindow *host) {
    return host->device;
}

void cc_windows_window_show(CcWindowsWindow *host) {
    PostMessageW(host->window, CC_WINDOW_SHOW, 0, 0);
}

void cc_windows_window_close(CcWindowsWindow *host) {
    if (!host)
        return;
    if (host->thread) {
        /* The GL owner has stopped presenting before this request. Destroying
         * the window on its own thread also ends an active move/size loop. */
        PostMessageW(host->window, CC_WINDOW_STOP, 0, 0);
        WaitForSingleObject(host->thread, INFINITE);
        CloseHandle(host->thread);
    }
    if (host->ready)
        CloseHandle(host->ready);
    DeleteCriticalSection(&host->mutex);
    free(host);
}

bool cc_windows_window_poll(CcWindowsWindow *host, CcEvent *event) {
    EnterCriticalSection(&host->mutex);
    bool found = true;
    if (host->quit) {
        host->quit = false;
        *event = (CcEvent){.type = CC_EVENT_QUIT};
    } else if (host->lost_events) {
        host->lost_events = false;
        *event = (CcEvent){.type = CC_EVENT_POINTER_LEAVE, .cancel_capture = true};
    } else if (host->resized) {
        host->resized = false;
        *event = (CcEvent){.type = CC_EVENT_WINDOW_RESIZED};
    } else if (host->count) {
        *event = host->events[host->read];
        host->read = (host->read + 1) % CC_WINDOW_EVENTS;
        --host->count;
    } else {
        found = false;
    }
    LeaveCriticalSection(&host->mutex);
    return found;
}

bool cc_windows_window_is_fullscreen(CcWindowsWindow *host) {
    EnterCriticalSection(&host->mutex);
    bool fullscreen = host->fullscreen;
    LeaveCriticalSection(&host->mutex);
    return fullscreen;
}

static bool set_style(HWND window, DWORD style) {
    SetLastError(ERROR_SUCCESS);
    return SetWindowLongPtrW(window, GWL_STYLE, (LONG_PTR)style) != 0 ||
           GetLastError() == ERROR_SUCCESS;
}

static bool change_fullscreen(CcWindowsWindow *host, bool fullscreen) {
    if (host->fullscreen == fullscreen)
        return true;
    bool okay;
    if (fullscreen) {
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        host->window_style = (DWORD)GetWindowLongPtrW(host->window, GWL_STYLE);
        host->placement.length = sizeof(host->placement);
        if (!GetWindowPlacement(host->window, &host->placement) ||
            !GetMonitorInfoW(MonitorFromWindow(host->window, MONITOR_DEFAULTTONEAREST),
                             &monitor))
            return false;
        okay =
            set_style(host->window, host->window_style & ~(DWORD)WS_OVERLAPPEDWINDOW) &&
            SetWindowPos(host->window, HWND_TOP, monitor.rcMonitor.left,
                         monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        if (!okay) {
            set_style(host->window, host->window_style);
            SetWindowPlacement(host->window, &host->placement);
        }
    } else {
        okay = set_style(host->window, host->window_style) &&
               SetWindowPlacement(host->window, &host->placement) &&
               SetWindowPos(host->window, NULL, 0, 0, 0, 0,
                            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
    if (okay) {
        EnterCriticalSection(&host->mutex);
        host->fullscreen = fullscreen;
        host->resized = true;
        LeaveCriticalSection(&host->mutex);
    }
    return okay;
}

bool cc_windows_window_set_fullscreen(CcWindowsWindow *host, bool fullscreen) {
    DWORD_PTR result = 0;
    /* Never wait indefinitely for a busy or failed native message thread. */
    return SendMessageTimeoutW(host->window, CC_WINDOW_FULLSCREEN, fullscreen, 0,
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result) &&
           result;
}

void cc_windows_window_size(CcWindowsWindow *host, int *width, int *height) {
    EnterCriticalSection(&host->mutex);
    *width = host->width > 0 ? host->width : 1;
    *height = host->height > 0 ? host->height : 1;
    LeaveCriticalSection(&host->mutex);
}
