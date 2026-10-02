#include "console_common/support/endian.h"

#include <assert.h>
#include <math.h>
#include <string.h>

int main(void) {
    const uint8_t words[] = {0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    assert(cc_read_be16(words + 1) == UINT16_C(0x1234));
    assert(cc_read_be32(words + 1) == UINT32_C(0x12345678));
    assert(cc_read_be64(words + 1) == UINT64_C(0x123456789abcdef0));
    assert(cc_read_le32(words + 1) == UINT32_C(0x78563412));
    uint8_t output[8];
    cc_write_be64(output, UINT64_C(0x123456789abcdef0));
    assert(!memcmp(output, words + 1, sizeof(output)));
    cc_write_be16(output, UINT16_C(0x1234));
    assert(!memcmp(output, words + 1, 2));
    cc_write_be32(output, UINT32_C(0x12345678));
    assert(!memcmp(output, words + 1, 4));
    cc_write_le32(output, UINT32_C(0x78563412));
    assert(!memcmp(output, words + 1, 4));
    const uint8_t floats[][4] = {{0x3f, 0x80, 0, 0},
                                 {0x80, 0, 0, 0},
                                 {0x7f, 0x80, 0, 0},
                                 {0x7f, 0xc0, 0x12, 0x34}};
    assert(cc_read_be_float(floats[0]) == 1);
    float negative_zero = cc_read_be_float(floats[1]);
    assert(negative_zero == 0 && signbit(negative_zero));
    assert(isinf(cc_read_be_float(floats[2])));
    float nan = cc_read_be_float(floats[3]);
    assert(isnan(nan));
    uint32_t bits;
    memcpy(&bits, &nan, sizeof(bits));
    assert(bits == UINT32_C(0x7fc01234));
    return 0;
}
