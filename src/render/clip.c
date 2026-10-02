#include "clip.h"

#include <math.h>

static int clip_edge(double coordinate, double scale, int limit, bool upper) {
    double value = coordinate * scale;
    if (value <= 0.0)
        return 0;
    if (value >= (double)limit)
        return limit;
    return (int)(upper ? ceil(value) : floor(value));
}

CcViewport cc_render_clip_pixels(const CcClipRect *clip, int output_width,
                                 int output_height) {
    if (output_width <= 0 || output_height <= 0)
        return (CcViewport){0};
    if (!clip)
        return (CcViewport){0, 0, output_width, output_height};
    if (!isfinite(clip->x) || !isfinite(clip->y) || !isfinite(clip->width) ||
        !isfinite(clip->height) || clip->width <= 0 || clip->height <= 0)
        return (CcViewport){0};

    /* Double precision gives both backends the same fractional-scale rounding
     * and keeps finite float endpoints from overflowing before clamping. */
    double scale_x = (double)output_width / CC_FRAME_WIDTH;
    double scale_y = (double)output_height / CC_FRAME_HEIGHT;
    int left = clip_edge(clip->x, scale_x, output_width, false);
    int top = clip_edge(clip->y, scale_y, output_height, false);
    int right = clip_edge((double)clip->x + clip->width, scale_x, output_width, true);
    int bottom =
        clip_edge((double)clip->y + clip->height, scale_y, output_height, true);
    return (CcViewport){left, top, right - left, bottom - top};
}
