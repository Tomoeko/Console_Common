#include <windows.h>
#include <assert.h>
#include <stdio.h>

static HANDLE paint_entered;
static HANDLE paint_release;
static volatile LONG block_paint;

static HDC WINAPI test_begin_paint(HWND window, LPPAINTSTRUCT paint) {
    if (InterlockedExchange(&block_paint, 0)) {
        SetEvent(paint_entered);
        assert(WaitForSingleObject(paint_release, 3000) == WAIT_OBJECT_0);
    }
    return BeginPaint(window, paint);
}

#define BeginPaint test_begin_paint
#include "platform/windows/window.c"
#undef BeginPaint

static void wait_visible(CcWindowsWindow *window) {
    for (unsigned attempt = 0; attempt < 100 && !IsWindowVisible(window->window);
         ++attempt)
        Sleep(10);
    assert(IsWindowVisible(window->window));
}

int main(void) {
    paint_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    paint_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(paint_entered && paint_release);
    CcWindowsWindow *window = cc_windows_window_open("Window regression", 640, 480);
    assert(window);
    cc_windows_window_show(window);
    wait_visible(window);
    RECT original;
    assert(GetWindowRect(window->window, &original));
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        assert(cc_windows_window_set_fullscreen(window, true));
        assert(cc_windows_window_is_fullscreen(window));
        assert(IsWindowVisible(window->window));
        assert(!(GetWindowLongPtrW(window->window, GWL_STYLE) & WS_OVERLAPPEDWINDOW));
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        assert(GetMonitorInfoW(
            MonitorFromWindow(window->window, MONITOR_DEFAULTTONEAREST), &monitor));
        RECT bounds;
        assert(GetWindowRect(window->window, &bounds));
        assert(EqualRect(&bounds, &monitor.rcMonitor));
        int width, height;
        cc_windows_window_size(window, &width, &height);
        assert(width == bounds.right - bounds.left);
        assert(height == bounds.bottom - bounds.top);
        assert(cc_windows_window_set_fullscreen(window, false));
        assert(!cc_windows_window_is_fullscreen(window));
        assert(IsWindowVisible(window->window));
        assert(GetWindowRect(window->window, &bounds));
        assert(EqualRect(&bounds, &original));
    }
    /* A native move/size loop blocks this same message thread. Polling and
     * rendering must remain independent of it; simulate the blocked owner. */
    InterlockedExchange(&block_paint, 1);
    assert(InvalidateRect(window->window, NULL, FALSE));
    assert(WaitForSingleObject(paint_entered, 3000) == WAIT_OBJECT_0);
    LARGE_INTEGER start, finish, frequency;
    assert(QueryPerformanceFrequency(&frequency));
    assert(QueryPerformanceCounter(&start));
    for (unsigned frame = 0; frame < 100; ++frame) {
        int width, height;
        CcEvent event;
        cc_windows_window_size(window, &width, &height);
        while (cc_windows_window_poll(window, &event)) {
        }
        assert(width == 640 && height == 480);
    }
    assert(QueryPerformanceCounter(&finish));
    assert(finish.QuadPart - start.QuadPart < frequency.QuadPart / 4);
    SetEvent(paint_release);
    cc_windows_window_close(window);
    CloseHandle(paint_entered);
    CloseHandle(paint_release);
    puts("Windows fullscreen, resize and message-thread isolation passed.");
    return 0;
}
