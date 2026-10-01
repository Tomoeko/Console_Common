#ifndef CONSOLE_COMMON_VIEWPORT_H
#define CONSOLE_COMMON_VIEWPORT_H

#include <stdbool.h>

/* Top-left output rectangle fitted to the consumer's displayed aspect ratio.
 * The logical framebuffer may be anamorphic and fills this rectangle. */
typedef struct CcViewport {
    int x;
    int y;
    int width;
    int height;
} CcViewport;

CcViewport cc_viewport_fit(int output_width, int output_height);
bool cc_viewport_map_pointer(CcViewport viewport, int x, int y, int *menu_x,
                             int *menu_y);
/* Writes logical coordinates even in the letterbox or outside the window.
 * Returns true only inside the fitted picture. Invalid viewports and output
 * pointers return false without writing coordinates. */
bool cc_viewport_map_pointer_unbounded(CcViewport viewport, int x, int y, int *menu_x,
                                       int *menu_y);

#endif
