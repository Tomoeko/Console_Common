#include "console_common/support/affine.h"

#include <assert.h>

static void expect_matrix(const float actual[12], const float expected[12]) {
    for (unsigned index = 0; index < 12; ++index)
        assert(actual[index] == expected[index]);
}

int main(void) {
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const float left[12] = {1, 2, 0, 3, 0, 1, 1, 4, 2, 0, 1, 5};
    const float right[12] = {2, 0, 1, 6, 1, 1, 0, 7, 0, 2, 1, 8};
    const float product[12] = {4, 2, 1, 23, 1, 3, 1, 19, 4, 2, 3, 25};
    float output[12];

    cc_affine_identity(output);
    expect_matrix(output, identity);
    cc_affine_multiply(output, identity, left);
    expect_matrix(output, left);
    cc_affine_multiply(output, right, identity);
    expect_matrix(output, right);
    cc_affine_multiply(output, left, right);
    expect_matrix(output, product);

    memcpy(output, left, sizeof(output));
    cc_affine_multiply(output, output, right);
    expect_matrix(output, product);
    memcpy(output, right, sizeof(output));
    cc_affine_multiply(output, left, output);
    expect_matrix(output, product);

    cc_affine_multiply(output, right, left);
    assert(output[3] == 17 && output[7] == 14 && output[11] == 21);
    return 0;
}
