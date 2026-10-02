#include "capture_jpeg.h"

#include <stdlib.h>
#include <string.h>

/* A component needs at most 17 DC bits plus 63*(10 code + 10 value) AC bits.
 * Three components and worst-case FF stuffing bound each 8x8 MCU at 960 bytes.
 * The separate header allowance also contains the final entropy pad and EOI.
 */
enum {
    JPEG_BLOCK_SIDE = 8,
    JPEG_BLOCK_VALUES = 64,
    JPEG_HEADER_CAPACITY = 1024,
    JPEG_BLOCK_CAPACITY = 960,
    JPEG_MAX_DIMENSION = 4096
};

typedef struct {
    uint16_t codes[256];
    uint8_t lengths[256];
    uint8_t counts[16];
    uint8_t symbols[162];
    unsigned symbol_count;
} JpegHuffman;

struct CcCaptureJpeg {
    uint8_t *output;
    uint8_t *previous;
    size_t output_capacity;
    size_t output_size;
    size_t pixel_stride;
    unsigned width;
    unsigned height;
    uint8_t header[JPEG_HEADER_CAPACITY];
    size_t header_size;
    uint8_t quantization[2][JPEG_BLOCK_VALUES];
    float inverse_quantization[2][JPEG_BLOCK_VALUES];
    JpegHuffman dc;
    JpegHuffman ac;
    uint32_t pending_bits;
    unsigned pending_count;
    bool failed;
    bool previous_valid;
};

/* Quantization examples from ITU-T T.81 (1992), Annex K, scaled independently
 * from its baseline coding rules. Zigzag order follows Annex A.3.6.
 */
static const uint8_t quantization_examples[2][JPEG_BLOCK_SIDE][JPEG_BLOCK_SIDE] = {
    {
        {16, 11, 10, 16, 24, 40, 51, 61},
        {12, 12, 14, 19, 26, 58, 60, 55},
        {14, 13, 16, 24, 40, 57, 69, 56},
        {14, 17, 22, 29, 51, 87, 80, 62},
        {18, 22, 37, 56, 68, 109, 103, 77},
        {24, 35, 55, 64, 81, 104, 113, 92},
        {49, 64, 78, 87, 103, 121, 120, 101},
        {72, 92, 95, 98, 112, 100, 103, 99},
    },
    {
        {17, 18, 24, 47, 99, 99, 99, 99},
        {18, 21, 26, 66, 99, 99, 99, 99},
        {24, 26, 56, 99, 99, 99, 99, 99},
        {47, 66, 99, 99, 99, 99, 99, 99},
        {99, 99, 99, 99, 99, 99, 99, 99},
        {99, 99, 99, 99, 99, 99, 99, 99},
        {99, 99, 99, 99, 99, 99, 99, 99},
        {99, 99, 99, 99, 99, 99, 99, 99},
    },
};

static const uint8_t zigzag[JPEG_BLOCK_VALUES] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static void append_byte(CcCaptureJpeg *encoder, uint8_t value) {
    if (encoder->output_size >= encoder->output_capacity) {
        encoder->failed = true;
        return;
    }
    encoder->output[encoder->output_size++] = value;
}

static void append_word(CcCaptureJpeg *encoder, unsigned value) {
    append_byte(encoder, (uint8_t)(value >> 8));
    append_byte(encoder, (uint8_t)value);
}

static void marker(CcCaptureJpeg *encoder, unsigned value) {
    append_byte(encoder, 0xff);
    append_byte(encoder, (uint8_t)value);
}

static bool build_huffman(JpegHuffman *table) {
    unsigned code = 0;
    unsigned symbol = 0;
    for (unsigned length = 1; length <= 16; ++length) {
        for (unsigned index = 0; index < table->counts[length - 1]; ++index) {
            if (symbol >= table->symbol_count || code >= (1u << length) - 1)
                return false;
            unsigned value = table->symbols[symbol++];
            if (table->lengths[value])
                return false;
            table->codes[value] = (uint16_t)code++;
            table->lengths[value] = (uint8_t)length;
        }
        code <<= 1;
    }
    return symbol == table->symbol_count;
}

static bool initialize_huffman(CcCaptureJpeg *encoder) {
    /* These authored tables favour low-frequency coefficients and end-of-block
     * without requiring a statistics pass. Codes follow T.81 Annex C; no code
     * is all ones, so entropy padding cannot become another symbol.
     */
    encoder->dc.counts[1] = 1;
    encoder->dc.counts[2] = 2;
    encoder->dc.counts[3] = 3;
    encoder->dc.counts[4] = 4;
    encoder->dc.counts[5] = 2;
    encoder->dc.symbol_count = 12;
    for (unsigned index = 0; index < 12; ++index)
        encoder->dc.symbols[index] = (uint8_t)index;
    encoder->ac.counts[1] = 1;
    for (unsigned length = 3; length <= 7; ++length)
        encoder->ac.counts[length - 1] = 2;
    encoder->ac.counts[7] = 1;
    encoder->ac.counts[9] = 150;
    encoder->ac.symbols[0] = 0;
    for (unsigned size = 1; size <= 10; ++size)
        encoder->ac.symbols[size] = (uint8_t)size;
    encoder->ac.symbols[11] = 0xf0;
    unsigned index = 12;
    for (unsigned run = 1; run <= 15; ++run) {
        for (unsigned size = 1; size <= 10; ++size)
            encoder->ac.symbols[index++] = (uint8_t)(run * 16 + size);
    }
    encoder->ac.symbol_count = index;
    return build_huffman(&encoder->dc) && build_huffman(&encoder->ac);
}

static void write_huffman(CcCaptureJpeg *encoder, const JpegHuffman *table,
                          unsigned id) {
    marker(encoder, 0xc4);
    append_word(encoder, 19 + table->symbol_count);
    append_byte(encoder, (uint8_t)id);
    for (unsigned length = 0; length < 16; ++length)
        append_byte(encoder, table->counts[length]);
    for (unsigned symbol = 0; symbol < table->symbol_count; ++symbol)
        append_byte(encoder, table->symbols[symbol]);
}

static bool make_header(CcCaptureJpeg *encoder) {
    marker(encoder, 0xd8);
    marker(encoder, 0xe0);
    append_word(encoder, 16);
    static const uint8_t jfif[] = {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    for (unsigned index = 0; index < sizeof(jfif); ++index)
        append_byte(encoder, jfif[index]);
    marker(encoder, 0xdb);
    append_word(encoder, 132);
    for (unsigned table = 0; table < 2; ++table) {
        append_byte(encoder, (uint8_t)table);
        for (unsigned coefficient = 0; coefficient < JPEG_BLOCK_VALUES; ++coefficient)
            append_byte(encoder, encoder->quantization[table][zigzag[coefficient]]);
    }
    marker(encoder, 0xc0);
    append_word(encoder, 17);
    append_byte(encoder, 8);
    append_word(encoder, encoder->height);
    append_word(encoder, encoder->width);
    append_byte(encoder, 3);
    for (unsigned component = 1; component <= 3; ++component) {
        append_byte(encoder, (uint8_t)component);
        append_byte(encoder, 0x11);
        append_byte(encoder, component == 1 ? 0 : 1);
    }
    write_huffman(encoder, &encoder->dc, 0);
    write_huffman(encoder, &encoder->ac, 0x10);
    marker(encoder, 0xda);
    append_word(encoder, 12);
    append_byte(encoder, 3);
    for (unsigned component = 1; component <= 3; ++component) {
        append_byte(encoder, (uint8_t)component);
        append_byte(encoder, 0);
    }
    append_byte(encoder, 0);
    append_byte(encoder, 63);
    append_byte(encoder, 0);
    if (encoder->failed || encoder->output_size > sizeof(encoder->header))
        return false;
    encoder->header_size = encoder->output_size;
    memcpy(encoder->header, encoder->output, encoder->header_size);
    return true;
}

CcCaptureJpeg *cc_capture_jpeg_open(unsigned width, unsigned height, unsigned quality) {
    if (!width || !height || width > JPEG_MAX_DIMENSION ||
        height > JPEG_MAX_DIMENSION || !quality || quality > 100)
        return NULL;
    size_t columns = ((size_t)width + 7) / JPEG_BLOCK_SIDE;
    size_t rows = ((size_t)height + 7) / JPEG_BLOCK_SIDE;
    if (columns > SIZE_MAX / rows ||
        columns * rows > (SIZE_MAX - JPEG_HEADER_CAPACITY) / JPEG_BLOCK_CAPACITY ||
        (size_t)width > SIZE_MAX / 4 / height)
        return NULL;
    CcCaptureJpeg *encoder = calloc(1, sizeof(*encoder));
    if (!encoder)
        return NULL;
    encoder->width = width;
    encoder->height = height;
    encoder->pixel_stride = (size_t)width * 4;
    encoder->output_capacity =
        columns * rows * JPEG_BLOCK_CAPACITY + JPEG_HEADER_CAPACITY;
    encoder->output = malloc(encoder->output_capacity);
    encoder->previous = malloc(encoder->pixel_stride * height);
    if (!encoder->output || !encoder->previous)
        goto release_encoder;
    unsigned scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (unsigned table = 0; table < 2; ++table) {
        for (unsigned index = 0; index < JPEG_BLOCK_VALUES; ++index) {
            unsigned value = (quantization_examples[table][index / JPEG_BLOCK_SIDE]
                                                   [index % JPEG_BLOCK_SIDE] *
                                  scale +
                              50) /
                             100;
            if (value < 1)
                value = 1;
            if (value > 255)
                value = 255;
            encoder->quantization[table][index] = (uint8_t)value;
            encoder->inverse_quantization[table][index] = 1.0f / (float)value;
        }
    }
    if (!initialize_huffman(encoder) || !make_header(encoder))
        goto release_encoder;
    return encoder;

release_encoder:
    cc_capture_jpeg_close(encoder);
    return NULL;
}

static void entropy_bits(CcCaptureJpeg *encoder, unsigned value, unsigned count) {
    if (!count || count > 16 || encoder->pending_count > 7) {
        encoder->failed = true;
        return;
    }
    encoder->pending_bits =
        (encoder->pending_bits << count) | (value & ((1u << count) - 1));
    encoder->pending_count += count;
    while (encoder->pending_count >= 8) {
        encoder->pending_count -= 8;
        uint8_t byte = (uint8_t)(encoder->pending_bits >> encoder->pending_count);
        append_byte(encoder, byte);
        if (byte == 0xff)
            append_byte(encoder, 0);
    }
}

static void entropy_symbol(CcCaptureJpeg *encoder, const JpegHuffman *table,
                           unsigned symbol) {
    if (symbol >= 256 || !table->lengths[symbol]) {
        encoder->failed = true;
        return;
    }
    entropy_bits(encoder, table->codes[symbol], table->lengths[symbol]);
}

static unsigned magnitude_size(int value) {
    unsigned magnitude = (unsigned)(value < 0 ? -value : value);
    unsigned size = 0;
    while (magnitude) {
        magnitude >>= 1;
        ++size;
    }
    return size;
}

static void entropy_magnitude(CcCaptureJpeg *encoder, int value, unsigned size) {
    if (!size)
        return;
    unsigned bits = (unsigned)(value < 0 ? value - 1 : value);
    entropy_bits(encoder, bits, size);
}

static void transform_eight(const float source[8], float target[8]) {
    /* Pair symmetry reduces the orthonormal T.81 Annex A.3.3 transform to an
     * even butterfly and four odd-frequency dot products. Each pass contains
     * its normalization, so quantization does not depend on a hidden scale.
     */
    float sum0 = source[0] + source[7];
    float sum1 = source[1] + source[6];
    float sum2 = source[2] + source[5];
    float sum3 = source[3] + source[4];
    float difference0 = source[0] - source[7];
    float difference1 = source[1] - source[6];
    float difference2 = source[2] - source[5];
    float difference3 = source[3] - source[4];
    float even0 = sum0 + sum3;
    float even1 = sum1 + sum2;
    float even2 = sum0 - sum3;
    float even3 = sum1 - sum2;
    target[0] = (even0 + even1) * 0.353553391f;
    target[4] = (even0 - even1) * 0.353553391f;
    target[2] = even2 * 0.461939766f + even3 * 0.191341716f;
    target[6] = even2 * 0.191341716f - even3 * 0.461939766f;
    target[1] = difference0 * 0.490392640f + difference1 * 0.415734806f +
                difference2 * 0.277785117f + difference3 * 0.097545161f;
    target[3] = difference0 * 0.415734806f - difference1 * 0.097545161f -
                difference2 * 0.490392640f - difference3 * 0.277785117f;
    target[5] = difference0 * 0.277785117f - difference1 * 0.490392640f +
                difference2 * 0.097545161f + difference3 * 0.415734806f;
    target[7] = difference0 * 0.097545161f - difference1 * 0.277785117f +
                difference2 * 0.415734806f - difference3 * 0.490392640f;
}

static void quantize_block(const float block[64], const float inverse[64],
                           int coefficients[64]) {
    float horizontal[64];
    for (unsigned row = 0; row < JPEG_BLOCK_SIDE; ++row)
        transform_eight(block + row * JPEG_BLOCK_SIDE,
                        horizontal + row * JPEG_BLOCK_SIDE);
    for (unsigned column = 0; column < JPEG_BLOCK_SIDE; ++column) {
        float source[8];
        float target[8];
        for (unsigned row = 0; row < JPEG_BLOCK_SIDE; ++row)
            source[row] = horizontal[row * JPEG_BLOCK_SIDE + column];
        transform_eight(source, target);
        for (unsigned row = 0; row < JPEG_BLOCK_SIDE; ++row) {
            unsigned index = row * JPEG_BLOCK_SIDE + column;
            float value = target[row] * inverse[index];
            coefficients[index] = (int)(value < 0 ? value - 0.5f : value + 0.5f);
        }
    }
}

static void encode_block(CcCaptureJpeg *encoder, const float block[64], unsigned table,
                         int *previous_dc) {
    int coefficients[64];
    quantize_block(block, encoder->inverse_quantization[table], coefficients);
    int difference = coefficients[0] - *previous_dc;
    *previous_dc = coefficients[0];
    unsigned size = magnitude_size(difference);
    if (size > 11) {
        encoder->failed = true;
        return;
    }
    entropy_symbol(encoder, &encoder->dc, size);
    entropy_magnitude(encoder, difference, size);
    unsigned zero_run = 0;
    for (unsigned index = 1; index < JPEG_BLOCK_VALUES; ++index) {
        int value = coefficients[zigzag[index]];
        if (!value) {
            ++zero_run;
            continue;
        }
        while (zero_run >= 16) {
            entropy_symbol(encoder, &encoder->ac, 0xf0);
            zero_run -= 16;
        }
        size = magnitude_size(value);
        if (size > 10) {
            encoder->failed = true;
            return;
        }
        entropy_symbol(encoder, &encoder->ac, zero_run * 16 + size);
        entropy_magnitude(encoder, value, size);
        zero_run = 0;
    }
    if (zero_run)
        entropy_symbol(encoder, &encoder->ac, 0);
}

static void load_block(const CcCaptureJpeg *encoder, const uint8_t *rgba, size_t stride,
                       unsigned left, unsigned top, float blocks[3][64]) {
    for (unsigned y = 0; y < JPEG_BLOCK_SIDE; ++y) {
        unsigned source_y = top + y < encoder->height ? top + y : encoder->height - 1;
        const uint8_t *row = rgba + (size_t)source_y * stride;
        for (unsigned x = 0; x < JPEG_BLOCK_SIDE; ++x) {
            unsigned source_x =
                left + x < encoder->width ? left + x : encoder->width - 1;
            const uint8_t *pixel = row + (size_t)source_x * 4;
            float red = pixel[0];
            float green = pixel[1];
            float blue = pixel[2];
            unsigned index = y * JPEG_BLOCK_SIDE + x;
            blocks[0][index] = 0.299f * red + 0.587f * green + 0.114f * blue - 128;
            blocks[1][index] = -0.168735892f * red - 0.331264108f * green + 0.5f * blue;
            blocks[2][index] = 0.5f * red - 0.418687589f * green - 0.081312411f * blue;
        }
    }
}

static bool previous_frame_matches(const CcCaptureJpeg *encoder, const uint8_t *rgba,
                                   size_t stride) {
    if (!encoder->previous_valid)
        return false;
    for (unsigned row = 0; row < encoder->height; ++row) {
        if (memcmp(rgba + (size_t)row * stride,
                   encoder->previous + (size_t)row * encoder->pixel_stride,
                   encoder->pixel_stride))
            return false;
    }
    return true;
}

bool cc_capture_jpeg_encode(CcCaptureJpeg *encoder, const uint8_t *rgba,
                            size_t row_stride, const uint8_t **sample, size_t *size) {
    if (!encoder || !rgba || !sample || !size || row_stride < encoder->pixel_stride ||
        row_stride > SIZE_MAX / encoder->height)
        return false;
    if (previous_frame_matches(encoder, rgba, row_stride)) {
        *sample = encoder->output;
        *size = encoder->output_size;
        return true;
    }
    encoder->output_size = encoder->header_size;
    memcpy(encoder->output, encoder->header, encoder->header_size);
    encoder->pending_bits = 0;
    encoder->pending_count = 0;
    encoder->failed = false;
    encoder->previous_valid = false;
    int previous_dc[3] = {0};
    for (unsigned top = 0; top < encoder->height; top += JPEG_BLOCK_SIDE) {
        for (unsigned left = 0; left < encoder->width; left += JPEG_BLOCK_SIDE) {
            float blocks[3][64];
            load_block(encoder, rgba, row_stride, left, top, blocks);
            for (unsigned component = 0; component < 3; ++component)
                encode_block(encoder, blocks[component], component ? 1 : 0,
                             previous_dc + component);
        }
        if (encoder->failed)
            return false;
    }
    if (encoder->pending_count) {
        unsigned padding = 8 - encoder->pending_count;
        entropy_bits(encoder, (1u << padding) - 1, padding);
    }
    marker(encoder, 0xd9);
    if (encoder->failed)
        return false;
    for (unsigned row = 0; row < encoder->height; ++row)
        memcpy(encoder->previous + (size_t)row * encoder->pixel_stride,
               rgba + (size_t)row * row_stride, encoder->pixel_stride);
    encoder->previous_valid = true;
    *sample = encoder->output;
    *size = encoder->output_size;
    return true;
}

void cc_capture_jpeg_close(CcCaptureJpeg *encoder) {
    if (!encoder)
        return;
    free(encoder->output);
    free(encoder->previous);
    free(encoder);
}
