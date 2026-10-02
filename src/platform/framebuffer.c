#include "framebuffer.h"

#include <string.h>

bool cc_framebuffer_storage(int width, int height, size_t alignment, size_t *stride,
                            size_t *byte_count) {
    if (!stride || !byte_count || width <= 0 || height <= 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 || (size_t)width > SIZE_MAX / 4)
        return false;
    size_t row = (size_t)width * 4;
    if (row > SIZE_MAX - (alignment - 1))
        return false;
    row = (row + alignment - 1) & ~(alignment - 1);
    if ((size_t)height > SIZE_MAX / row)
        return false;
    *stride = row;
    *byte_count = row * (size_t)height;
    return true;
}

bool cc_framebuffer_rgba(uint8_t *destination, size_t capacity, const uint8_t *source,
                         int width, int height, size_t source_stride, bool bgra,
                         bool bottom_up) {
    size_t row = 0;
    size_t required = 0;
    if (!destination || !source ||
        !cc_framebuffer_storage(width, height, 1, &row, &required) ||
        required > capacity || source_stride < row ||
        (size_t)height > SIZE_MAX / source_stride)
        return false;
    for (int y = 0; y < height; ++y) {
        size_t source_y = (size_t)(bottom_up ? height - 1 - y : y);
        const uint8_t *input = source + source_y * source_stride;
        uint8_t *output = destination + (size_t)y * row;
        if (!bgra) {
            memcpy(output, input, row);
            continue;
        }
        for (int x = 0; x < width; ++x) {
            size_t pixel = (size_t)x * 4;
            output[pixel] = input[pixel + 2];
            output[pixel + 1] = input[pixel + 1];
            output[pixel + 2] = input[pixel];
            output[pixel + 3] = input[pixel + 3];
        }
    }
    return true;
}
