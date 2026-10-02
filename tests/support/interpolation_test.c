#include "console_common/support/interpolation.h"

#include <assert.h>

static void test_endpoints(void) {
    assert(cc_cubic_hermite(0, 8, 2, 3, 11, -4) == 2);
    assert(cc_cubic_hermite(8, 8, 2, 3, 11, -4) == 11);
    assert(cc_cubic_hermite(2, 4, 3, 0, 11, 0) == 7);
}

static void test_linear_motion(void) {
    const float times[] = {-8, -2, 0, 2, 4, 8, 10, 16};
    for (unsigned index = 0; index < sizeof(times) / sizeof(times[0]); ++index) {
        float time = times[index];
        assert(cc_cubic_hermite(time, 8, -4, 2, 12, 2) == -4 + 2 * time);
    }
}

static void test_tangent_units(void) {
    /* Quarter-time basis weights are exactly representable in binary. Only
     * the first tangent contributes here; doubling duration doubles displacement. */
    assert(cc_cubic_hermite(1, 4, 0, 1, 0, 0) == 0.5625f);
    assert(cc_cubic_hermite(2, 8, 0, 1, 0, 0) == 1.125f);
    /* A positive arrival slope approaches the endpoint from below. */
    assert(cc_cubic_hermite(6, 8, 0, 0, 0, 2) == -2.25f);
    assert(cc_cubic_hermite(4, 8, 0, 1, 0, 2) == -1);
}

static void test_extrapolation(void) {
    assert(cc_cubic_hermite(-4, 4, 0, 0, 1, 0) == 5);
    assert(cc_cubic_hermite(8, 4, 0, 0, 1, 0) == -4);
}

int main(void) {
    test_endpoints();
    test_linear_motion();
    test_tangent_units();
    test_extrapolation();
    return 0;
}
