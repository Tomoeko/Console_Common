#ifndef CONSOLE_COMMON_RENDER_CLIP_H
#define CONSOLE_COMMON_RENDER_CLIP_H

#include "console_common/platform/platform.h"
#include "console_common/render/viewport.h"

/* Returns a top-left pixel rectangle relative to the output content. Lower
 * edges round down and upper edges round up. NULL covers the entire content;
 * nonfinite or nonpositive rectangles cover nothing. Backend origin changes
 * and retained-frame intersections remain with their callers. */
CcViewport cc_render_clip_pixels(const CcClipRect *clip, int output_width,
                                 int output_height);

#endif
