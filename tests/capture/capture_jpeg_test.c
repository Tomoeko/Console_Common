#define _POSIX_C_SOURCE 200809L

#include "capture/capture_jpeg.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned read_word(const uint8_t *bytes) {
    return (unsigned)bytes[0] << 8 | bytes[1];
}

static void check_huffman(const uint8_t *bytes, size_t size) {
    assert(size >= 17 && (bytes[0] == 0 || bytes[0] == 0x10));
    unsigned code = 0;
    size_t symbols = 0;
    for (unsigned length = 1; length <= 16; ++length) {
        unsigned count = bytes[length];
        assert(count < (1u << length) - code);
        code = (code + count) << 1;
        symbols += count;
    }
    assert(size == 17 + symbols);
    bool seen[256] = {false};
    for (size_t index = 0; index < symbols; ++index) {
        unsigned symbol = bytes[17 + index];
        assert(!seen[symbol]);
        seen[symbol] = true;
    }
    if (bytes[0] == 0) {
        assert(symbols == 12);
        for (unsigned size_bits = 0; size_bits <= 11; ++size_bits)
            assert(seen[size_bits]);
    } else {
        assert(symbols == 162 && seen[0] && seen[0xf0]);
        for (unsigned run = 0; run <= 15; ++run) {
            for (unsigned size_bits = 1; size_bits <= 10; ++size_bits)
                assert(seen[run * 16 + size_bits]);
        }
    }
}

static void check_jpeg(const uint8_t *bytes, size_t size, unsigned width,
                       unsigned height) {
    assert(size > 32 && bytes[0] == 0xff && bytes[1] == 0xd8);
    size_t offset = 2;
    unsigned seen = 0;
    while (offset < size) {
        assert(size - offset >= 4 && bytes[offset] == 0xff);
        unsigned marker = bytes[offset + 1];
        unsigned length = read_word(bytes + offset + 2);
        assert(length >= 2 && length <= size - offset - 2);
        const uint8_t *payload = bytes + offset + 4;
        unsigned payload_size = length - 2;
        if (marker == 0xe0) {
            assert(payload_size == 14 && !memcmp(payload, "JFIF", 5));
            seen |= 1;
        } else if (marker == 0xdb) {
            assert(payload_size == 130 && payload[0] == 0 && payload[65] == 1);
            for (unsigned index = 0; index < 64; ++index)
                assert(payload[1 + index] && payload[66 + index]);
            seen |= 2;
        } else if (marker == 0xc0) {
            assert(payload_size == 15 && payload[0] == 8 &&
                   read_word(payload + 1) == height &&
                   read_word(payload + 3) == width && payload[5] == 3);
            for (unsigned component = 0; component < 3; ++component) {
                assert(payload[6 + component * 3] == component + 1 &&
                       payload[7 + component * 3] == 0x11 &&
                       payload[8 + component * 3] == (component ? 1 : 0));
            }
            seen |= 4;
        } else if (marker == 0xc4) {
            check_huffman(payload, payload_size);
            seen |= payload[0] ? 16u : 8u;
        } else {
            assert(marker == 0xda && payload_size == 10 && payload[0] == 3);
            assert(payload[7] == 0 && payload[8] == 63 && payload[9] == 0);
            offset += length + 2;
            break;
        }
        offset += length + 2;
    }
    assert(seen == 31 && offset < size - 2);
    while (offset < size - 2) {
        if (bytes[offset++] == 0xff) {
            assert(offset < size - 2 && bytes[offset] == 0);
            ++offset;
        }
    }
    assert(offset == size - 2 && bytes[offset] == 0xff && bytes[offset + 1] == 0xd9);
}

static void fill_image(uint8_t *rgba, unsigned width, unsigned height, size_t stride,
                       unsigned frame) {
    memset(rgba, 0xa5, stride * height);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint8_t *pixel = rgba + (size_t)y * stride + (size_t)x * 4;
            bool text = y > height / 3 && y < height / 3 + height / 8 &&
                        ((x + frame) / 4 % 5 == 0 || y / 4 % 5 == 0);
            pixel[0] = text ? 255 : (uint8_t)((x + frame) * 255 / width);
            pixel[1] = text ? 255 : (uint8_t)(y * 255 / height);
            pixel[2] = text ? 255 : (uint8_t)(64 + (x + y + frame) % 128);
            pixel[3] = (uint8_t)(x + y);
        }
    }
}

static void make_path(char path[1024], const char *directory, unsigned width,
                      unsigned height, const char *extension) {
    int length =
        snprintf(path, 1024, "%s/jpeg-%ux%u.%s", directory, width, height, extension);
    assert(length > 0 && length < 1024);
}

static double elapsed_seconds(struct timespec first, struct timespec last) {
    return (double)(last.tv_sec - first.tv_sec) +
           (double)(last.tv_nsec - first.tv_nsec) / 1000000000.0;
}

static void test_frame(const char *directory, unsigned width, unsigned height) {
    CcCaptureJpeg *encoder = cc_capture_jpeg_open(width, height, 97);
    assert(encoder);
    size_t stride = (size_t)width * 4 + 16;
    uint8_t *rgba = malloc(stride * height);
    assert(rgba);
    fill_image(rgba, width, height, stride, 0);
    const uint8_t *sample = NULL;
    size_t size = 0;
    assert(!cc_capture_jpeg_encode(NULL, rgba, stride, &sample, &size));
    assert(!cc_capture_jpeg_encode(encoder, NULL, stride, &sample, &size));
    assert(!cc_capture_jpeg_encode(encoder, rgba, stride, NULL, &size));
    assert(!cc_capture_jpeg_encode(encoder, rgba, stride, &sample, NULL));
    assert(
        !cc_capture_jpeg_encode(encoder, rgba, (size_t)width * 4 - 1, &sample, &size));
    if (height > 1)
        assert(!cc_capture_jpeg_encode(encoder, rgba, SIZE_MAX, &sample, &size));
    assert(cc_capture_jpeg_encode(encoder, rgba, stride, &sample, &size));
    check_jpeg(sample, size, width, height);
    uint8_t *reference = malloc(size);
    assert(reference);
    memcpy(reference, sample, size);
    const uint8_t *repeated = NULL;
    size_t repeated_size = 0;
    assert(cc_capture_jpeg_encode(encoder, rgba, stride, &repeated, &repeated_size));
    assert(repeated == sample && repeated_size == size &&
           !memcmp(reference, sample, size));
    char path[1024];
    make_path(path, directory, width, height, "jpg");
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(sample, 1, size, file) == size && fclose(file) == 0);
    make_path(path, directory, width, height, "rgb");
    file = fopen(path, "wb");
    assert(file);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x)
            assert(fwrite(rgba + (size_t)y * stride + (size_t)x * 4, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
    if (width >= 640) {
        const unsigned frames = 12;
        struct timespec first;
        struct timespec last;
        double elapsed = 0;
        size_t total = 0;
        for (unsigned frame = 1; frame <= frames; ++frame) {
            fill_image(rgba, width, height, stride, frame);
            assert(clock_gettime(CLOCK_MONOTONIC, &first) == 0);
            assert(cc_capture_jpeg_encode(encoder, rgba, stride, &sample, &size));
            assert(clock_gettime(CLOCK_MONOTONIC, &last) == 0);
            elapsed += elapsed_seconds(first, last);
            total += size;
        }
        printf("JPEG %ux%u: %.3f ms/frame, %zu bytes/frame, %.2f MiB/s at60Hz\n", width,
               height, elapsed * 1000 / frames, total / frames,
               (double)total / frames * 60 / (1024 * 1024));
        assert(clock_gettime(CLOCK_MONOTONIC, &first) == 0);
        for (unsigned frame = 0; frame < 120; ++frame)
            assert(cc_capture_jpeg_encode(encoder, rgba, stride, &sample, &size));
        assert(clock_gettime(CLOCK_MONOTONIC, &last) == 0);
        printf("JPEG %ux%u repeated frame: %.3f ms/frame\n", width, height,
               elapsed_seconds(first, last) * 1000 / 120);
    }
    free(reference);
    free(rgba);
    cc_capture_jpeg_close(encoder);
}

static void test_extreme_coefficients(void) {
    CcCaptureJpeg *encoder = cc_capture_jpeg_open(16, 16, 100);
    assert(encoder);
    uint8_t rgba[16 * 16 * 4];
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned y = 0; y < 16; ++y) {
            for (unsigned x = 0; x < 16; ++x) {
                uint8_t *pixel = rgba + (y * 16 + x) * 4;
                unsigned value = mode == 0   ? (x < 8 ? 0 : 255)
                                 : mode == 1 ? (x + y) % 2 * 255
                                 : mode == 2 ? (x * 71 + y * 151) % 256
                                             : (x / 4 + y / 4) % 3 * 127;
                pixel[0] = (uint8_t)value;
                pixel[1] = mode == 3 ? (uint8_t)(255 - value) : (uint8_t)value;
                pixel[2] = (uint8_t)value;
                pixel[3] = 255;
            }
        }
        const uint8_t *sample;
        size_t size;
        assert(cc_capture_jpeg_encode(encoder, rgba, 64, &sample, &size));
        check_jpeg(sample, size, 16, 16);
    }
    cc_capture_jpeg_close(encoder);
    encoder = cc_capture_jpeg_open(16, 16, 1);
    assert(encoder);
    const uint8_t *sample;
    size_t size;
    assert(cc_capture_jpeg_encode(encoder, rgba, 64, &sample, &size));
    check_jpeg(sample, size, 16, 16);
    cc_capture_jpeg_close(encoder);
}

int main(int argc, char **argv) {
    assert(argc == 2 && argv[1][0]);
    assert(!cc_capture_jpeg_open(0, 1, 97));
    assert(!cc_capture_jpeg_open(1, 0, 97));
    assert(!cc_capture_jpeg_open(UINT_MAX, 1, 97));
    assert(!cc_capture_jpeg_open(1, UINT_MAX, 97));
    assert(!cc_capture_jpeg_open(4097, 1, 97));
    assert(!cc_capture_jpeg_open(1, 4097, 97));
    assert(!cc_capture_jpeg_open(1, 1, 0));
    assert(!cc_capture_jpeg_open(1, 1, 101));
    cc_capture_jpeg_close(NULL);
    test_extreme_coefficients();
    test_frame(argv[1], 1, 1);
    test_frame(argv[1], 7, 13);
    test_frame(argv[1], 19, 17);
    test_frame(argv[1], 640, 480);
    test_frame(argv[1], 1920, 1080);
    puts("Portable JPEG capture tests passed.");
    return 0;
}
