#include "console_common/render/indexed_strip.h"

#include "console_common/support/endian.h"

#include <stdint.h>

static bool span_valid(const void *pointer, size_t size) {
    return size == 0 || (pointer && (uintptr_t)pointer <= UINTPTR_MAX - size);
}

static bool spans_disjoint(const void *left, size_t left_size, const void *right,
                           size_t right_size) {
    if (left && left == right)
        return false;
    if (!left_size || !right_size)
        return true;
    uintptr_t first = (uintptr_t)left;
    uintptr_t second = (uintptr_t)right;
    return first >= second ? first - second >= right_size : second - first >= left_size;
}

static bool views_valid(const uint8_t *source, size_t source_size, size_t index_count,
                        const uint16_t *triangles, size_t triangle_capacity,
                        const size_t *written) {
    if (index_count > SIZE_MAX / 4 || index_count * 4 > source_size ||
        triangle_capacity > SIZE_MAX / sizeof(*triangles) || !written ||
        (uintptr_t)written % _Alignof(size_t) ||
        (triangles && (uintptr_t)triangles % _Alignof(uint16_t)))
        return false;
    size_t output_size = triangle_capacity * sizeof(*triangles);
    return span_valid(source, source_size) && span_valid(triangles, output_size) &&
           span_valid(written, sizeof(*written)) &&
           spans_disjoint(source, source_size, triangles, output_size) &&
           spans_disjoint(written, sizeof(*written), source, source_size) &&
           spans_disjoint(written, sizeof(*written), triangles, output_size);
}

static bool count_triangles(const uint8_t *source, size_t index_count,
                            size_t vertex_count, size_t *count) {
    size_t result = 0;
    unsigned prefix = 0;
    for (size_t index = 0; index < index_count; ++index) {
        uint32_t vertex = cc_read_be32(source + index * 4);
        if (vertex == UINT32_MAX) {
            prefix = 0;
        } else {
            if (vertex >= vertex_count)
                return false;
            if (prefix < 2) {
                ++prefix;
            } else {
                if (result > SIZE_MAX - 3)
                    return false;
                result += 3;
            }
        }
    }
    *count = result;
    return true;
}

static size_t write_triangles(const uint8_t *source, size_t index_count,
                              uint16_t *triangles) {
    uint16_t previous[2] = {0, 0};
    unsigned prefix = 0;
    bool odd = false;
    size_t count = 0;
    for (size_t index = 0; index < index_count; ++index) {
        uint32_t vertex = cc_read_be32(source + index * 4);
        if (vertex == UINT32_MAX) {
            prefix = 0;
            odd = false;
        } else if (prefix < 2) {
            previous[prefix++] = (uint16_t)vertex;
        } else {
            triangles[count++] = previous[odd ? 1 : 0];
            triangles[count++] = previous[odd ? 0 : 1];
            triangles[count++] = (uint16_t)vertex;
            previous[0] = previous[1];
            previous[1] = (uint16_t)vertex;
            odd = !odd;
        }
    }
    return count;
}

bool cc_indexed_strip_be32_to_triangles(const uint8_t *source, size_t source_size,
                                        size_t index_count, size_t vertex_count,
                                        uint16_t *triangles, size_t triangle_capacity,
                                        size_t *written) {
    size_t needed;
    if (!vertex_count || vertex_count > (size_t)UINT16_MAX + 1 ||
        !views_valid(source, source_size, index_count, triangles, triangle_capacity,
                     written) ||
        !count_triangles(source, index_count, vertex_count, &needed) ||
        needed > triangle_capacity)
        return false;
    *written = write_triangles(source, index_count, triangles);
    return true;
}
