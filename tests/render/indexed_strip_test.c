#include "console_common/render/indexed_strip.h"

#include "console_common/support/endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static void check_sequence(const uint32_t *indices, size_t count,
                           const uint16_t *expected, size_t expected_count) {
    uint8_t source[128];
    uint16_t output[64];
    CHECK(count <= sizeof(source) / 4);
    for (size_t index = 0; index < count; ++index)
        cc_write_be32(source + index * 4, indices[index]);
    memset(output, 0x9a, sizeof(output));
    size_t written = SIZE_MAX;
    CHECK(cc_indexed_strip_be32_to_triangles(source, count * 4, count, 8, output, 64,
                                             &written));
    CHECK(written == expected_count);
    CHECK(!expected_count || memcmp(output, expected, expected_count * 2) == 0);
    for (size_t index = expected_count; index < 64; ++index)
        CHECK(output[index] == 0x9a9a);
}

static void sequences(void) {
    const uint32_t strip[] = {0, 1, 2, 3, 4};
    const uint16_t triangles[] = {0, 1, 2, 2, 1, 3, 2, 3, 4};
    check_sequence(strip, 5, triangles, 9);
    const uint32_t restarts[] = {
        UINT32_MAX, 0, 1, 2, 3, UINT32_MAX, UINT32_MAX, 4, 5, 6, 7, UINT32_MAX, 0};
    const uint16_t restarted[] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    check_sequence(restarts, 13, restarted, 12);
    const uint32_t degenerates[] = {0, 1, 1, 2, 3};
    const uint16_t retained[] = {0, 1, 1, 1, 1, 2, 1, 2, 3};
    check_sequence(degenerates, 5, retained, 9);
    const uint32_t incomplete[] = {0, UINT32_MAX, 1, 2, UINT32_MAX, UINT32_MAX};
    check_sequence(incomplete, 6, NULL, 0);
}

static void failures(void) {
    _Alignas(size_t) uint8_t source[32] = {0};
    _Alignas(size_t) uint16_t output[16];
    for (unsigned index = 0; index < 5; ++index)
        cc_write_be32(source + index * 4, index);
    memset(output, 0xa7, sizeof(output));
    size_t written = 51;
    uint8_t before[sizeof(source)];
    memcpy(before, source, sizeof(before));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8, output, 8,
                                              &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, 19, 5, 8, output, 16, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 4, output, 16,
                                              &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 65537, output,
                                              16, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), SIZE_MAX, 8,
                                              output, 16, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8, output,
                                              SIZE_MAX, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles((const uint8_t *)(UINTPTR_MAX - 3), 20, 5,
                                              8, output, 16, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8,
                                              (uint16_t *)((uint8_t *)output + 1), 16,
                                              &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8, output, 16,
                                              (size_t *)((uint8_t *)&written + 1)));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8,
                                              (uint16_t *)source, 16, &written));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8, output, 16,
                                              (size_t *)source));
    CHECK(!cc_indexed_strip_be32_to_triangles(source, sizeof(source), 5, 8, output, 16,
                                              (size_t *)output));
    CHECK(written == 51 && memcmp(source, before, sizeof(source)) == 0);
    for (size_t index = 0; index < 16; ++index)
        CHECK(output[index] == 0xa7a7);
    CHECK(cc_indexed_strip_be32_to_triangles(NULL, 0, 0, 8, NULL, 0, &written));
    CHECK(written == 0);
    CHECK(!cc_indexed_strip_be32_to_triangles((const uint8_t *)&written, 0, 0, 8, NULL,
                                              0, &written));
    CHECK(written == 0);
}

int main(void) {
    sequences();
    failures();
    puts("BE32 strip parity/restart/degenerate/admission checks PASS");
    return 0;
}
