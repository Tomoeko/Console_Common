#ifndef CC_SUPPORT_AFFINE_H
#define CC_SUPPORT_AFFINE_H

#include <string.h>

/* Row-major 3x4 matrices act on column vectors with an implicit final row
 * of (0, 0, 0, 1). Output may alias either input. */
static inline void cc_affine_identity(float matrix[12]) {
    memset(matrix, 0, 12 * sizeof(*matrix));
    matrix[0] = matrix[5] = matrix[10] = 1;
}

static inline void cc_affine_multiply(float output[12], const float left[12],
                                      const float right[12]) {
    float result[12];
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            result[row * 4 + column] = 0;
            for (unsigned inner = 0; inner < 3; ++inner)
                result[row * 4 + column] +=
                    left[row * 4 + inner] * right[inner * 4 + column];
        }
        result[row * 4 + 3] = left[row * 4 + 3];
        for (unsigned inner = 0; inner < 3; ++inner)
            result[row * 4 + 3] += left[row * 4 + inner] * right[inner * 4 + 3];
    }
    memcpy(output, result, sizeof(result));
}

#endif
