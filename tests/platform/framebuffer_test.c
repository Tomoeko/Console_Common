#include "framebuffer.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void test_storage(void) {
    size_t stride = 0;
    size_t bytes = 0;
    assert(cc_framebuffer_storage(3, 2, 1, &stride, &bytes));
    assert(stride == 12 && bytes == 24);
    assert(cc_framebuffer_storage(3, 2, 256, &stride, &bytes));
    assert(stride == 256 && bytes == 512);
    assert(!cc_framebuffer_storage(0, 2, 1, &stride, &bytes));
    assert(!cc_framebuffer_storage(3, -1, 1, &stride, &bytes));
    assert(!cc_framebuffer_storage(3, 2, 0, &stride, &bytes));
    assert(!cc_framebuffer_storage(3, 2, 3, &stride, &bytes));
    assert(!cc_framebuffer_storage(3, 2, SIZE_MAX, &stride, &bytes));
    assert(!cc_framebuffer_storage(3, 2, 1, NULL, &bytes));
    assert(!cc_framebuffer_storage(3, 2, 1, &stride, NULL));
    assert(
        !cc_framebuffer_storage(INT_MAX, INT_MAX, (SIZE_MAX / 2) + 1, &stride, &bytes));
}

static void test_rows(void) {
    const uint8_t padded_bgra[] = {
        3,  2,  1, 4,  7,  6,  5,  8,  99, 99, 99, 99,
        11, 10, 9, 12, 15, 14, 13, 16, 99, 99, 99, 99,
    };
    const uint8_t rgba[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t reversed[] = {9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t output[sizeof(rgba)] = {0};
    assert(cc_framebuffer_rgba(output, sizeof(output), padded_bgra, 2, 2, 12, true,
                               false));
    assert(memcmp(output, rgba, sizeof(output)) == 0);
    assert(
        cc_framebuffer_rgba(output, sizeof(output), padded_bgra, 2, 2, 12, true, true));
    assert(memcmp(output, reversed, sizeof(output)) == 0);
    assert(cc_framebuffer_rgba(output, sizeof(output), rgba, 2, 2, 8, false, false));
    assert(memcmp(output, rgba, sizeof(output)) == 0);
    assert(cc_framebuffer_rgba(output, sizeof(output), rgba, 2, 2, 8, false, true));
    assert(memcmp(output, reversed, sizeof(output)) == 0);
    assert(
        !cc_framebuffer_rgba(output, sizeof(output) - 1, rgba, 2, 2, 8, false, false));
    assert(!cc_framebuffer_rgba(output, sizeof(output), rgba, 2, 2, 7, false, false));
    assert(!cc_framebuffer_rgba(output, sizeof(output), rgba, 2, 2, SIZE_MAX, false,
                                false));
    assert(!cc_framebuffer_rgba(NULL, sizeof(output), rgba, 2, 2, 8, false, false));
    assert(!cc_framebuffer_rgba(output, sizeof(output), NULL, 2, 2, 8, false, false));
}

int main(void) {
    test_storage();
    test_rows();
    return 0;
}
