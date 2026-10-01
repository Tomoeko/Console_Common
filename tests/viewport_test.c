#include "console_common/platform/platform.h"
#include "console_common/render/viewport.h"

#include <assert.h>
#include <limits.h>

int main(void) {
    CcViewport viewport = cc_viewport_fit(1200, 720);
#if CC_ASPECT_WIDTH == 16 && CC_ASPECT_HEIGHT == 9
    assert(viewport.x == 0 && viewport.y == 22);
    assert(viewport.width == 1200 && viewport.height == 675);
#elif CC_ASPECT_WIDTH == 4 && CC_ASPECT_HEIGHT == 3
    assert(viewport.x == 120 && viewport.y == 0);
    assert(viewport.width == 960 && viewport.height == 720);
#endif
    int x = -1;
    int y = -1;
    assert(cc_viewport_map_pointer(viewport, viewport.x, viewport.y, &x, &y));
    assert(x == 0 && y == 0);
    assert(cc_viewport_map_pointer(viewport, viewport.x + viewport.width / 2,
                                   viewport.y + viewport.height / 2, &x, &y));
    assert(x == CC_FRAME_WIDTH / 2);
    assert(y == CC_FRAME_HEIGHT / 2 || y == CC_FRAME_HEIGHT / 2 - 1);
    assert(!cc_viewport_map_pointer_unbounded(viewport, viewport.x - 1, viewport.y - 1,
                                              &x, &y));
    assert(x < 0 && y < 0);
    assert(!cc_viewport_map_pointer_unbounded(viewport, viewport.x + viewport.width,
                                              viewport.y + viewport.height, &x, &y));
    assert(x == CC_FRAME_WIDTH && y == CC_FRAME_HEIGHT);
    assert(cc_viewport_fit(0, 100).width == 0);
    assert(cc_viewport_fit(100, 0).height == 0);
    viewport = cc_viewport_fit(INT_MAX, INT_MAX);
    assert(viewport.width > 0 && viewport.height > 0);
    assert(viewport.width <= INT_MAX && viewport.height <= INT_MAX);
    return 0;
}
