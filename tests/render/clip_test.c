#include "clip.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>

static void assert_clip(const CcClipRect *clip, int width, int height,
                        CcViewport expected) {
    CcViewport actual = cc_render_clip_pixels(clip, width, height);
    assert(actual.x == expected.x && actual.y == expected.y);
    assert(actual.width == expected.width && actual.height == expected.height);
}

static void test_pixel_rounding(void) {
    assert_clip(NULL, 100, 50, (CcViewport){0, 0, 100, 50});
    CcClipRect clip = {1.25f, 2.75f, 3.5f, 4.5f};
    assert_clip(&clip, CC_FRAME_WIDTH, CC_FRAME_HEIGHT, (CcViewport){1, 2, 4, 6});
    assert_clip(&clip, 2 * CC_FRAME_WIDTH, 2 * CC_FRAME_HEIGHT,
                (CcViewport){2, 5, 8, 10});
    assert_clip(&clip, CC_FRAME_WIDTH / 2, CC_FRAME_HEIGHT / 2,
                (CcViewport){0, 1, 3, 3});
    clip = (CcClipRect){CC_FRAME_WIDTH / 4.0f, CC_FRAME_HEIGHT / 4.0f,
                        CC_FRAME_WIDTH / 8.0f, CC_FRAME_HEIGHT / 8.0f};
    assert_clip(&clip, 683, 521, (CcViewport){170, 130, 87, 66});
    clip = (CcClipRect){-2.5f, -4.25f, 4.5f, 6.25f};
    assert_clip(&clip, CC_FRAME_WIDTH, CC_FRAME_HEIGHT, (CcViewport){0, 0, 2, 2});
    clip = (CcClipRect){CC_FRAME_WIDTH - 1.25f, CC_FRAME_HEIGHT - 2.5f, 10, 10};
    assert_clip(&clip, CC_FRAME_WIDTH, CC_FRAME_HEIGHT,
                (CcViewport){CC_FRAME_WIDTH - 2, CC_FRAME_HEIGHT - 3, 2, 3});
    clip = (CcClipRect){CC_FRAME_WIDTH, CC_FRAME_HEIGHT, 10, 10};
    assert_clip(&clip, CC_FRAME_WIDTH, CC_FRAME_HEIGHT,
                (CcViewport){CC_FRAME_WIDTH, CC_FRAME_HEIGHT, 0, 0});
    clip = (CcClipRect){0, 0, CC_FRAME_WIDTH, CC_FRAME_HEIGHT};
    assert_clip(&clip, INT_MAX, INT_MAX, (CcViewport){0, 0, INT_MAX, INT_MAX});
}

static void test_invalid_and_large_values(void) {
    CcClipRect clip = {0, 0, 10, 10};
    assert_clip(&clip, 0, 100, (CcViewport){0});
    assert_clip(NULL, 100, -1, (CcViewport){0});
    clip.width = -10;
    assert_clip(&clip, 100, 100, (CcViewport){0});
    clip.width = 0;
    assert_clip(&clip, 100, 100, (CcViewport){0});
    clip = (CcClipRect){0, 0, 10, -1};
    assert_clip(&clip, 100, 100, (CcViewport){0});
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        clip = (CcClipRect){invalid[index], 0, 10, 10};
        assert_clip(&clip, 100, 100, (CcViewport){0});
        clip = (CcClipRect){0, invalid[index], 10, 10};
        assert_clip(&clip, 100, 100, (CcViewport){0});
        clip = (CcClipRect){0, 0, invalid[index], 10};
        assert_clip(&clip, 100, 100, (CcViewport){0});
        clip = (CcClipRect){0, 0, 10, invalid[index]};
        assert_clip(&clip, 100, 100, (CcViewport){0});
    }
    clip = (CcClipRect){FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    assert_clip(&clip, 100, 100, (CcViewport){100, 100, 0, 0});
    clip = (CcClipRect){-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX};
    assert_clip(&clip, 100, 100, (CcViewport){0});
    clip = (CcClipRect){0, 0, FLT_MAX, FLT_MAX};
    assert_clip(&clip, 100, 100, (CcViewport){0, 0, 100, 100});
}

int main(void) {
    test_pixel_rounding();
    test_invalid_and_large_values();
    return 0;
}
