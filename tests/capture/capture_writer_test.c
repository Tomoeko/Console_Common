#include "console_common/capture/capture_writer.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint64_t read_u64(const uint8_t *bytes) {
    return (uint64_t)read_u32(bytes) << 32 | read_u32(bytes + 4);
}

typedef struct {
    uint8_t *bytes;
    size_t size;
} TestFile;

static TestFile read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    return (TestFile){bytes, (size_t)length};
}

static const uint8_t *find_box(const uint8_t *bytes, size_t size, const char type[4],
                               size_t *box_size) {
    size_t offset = 0;
    while (size - offset >= 8) {
        uint32_t length = read_u32(bytes + offset);
        if (length < 8 || length > size - offset)
            return NULL;
        if (!memcmp(bytes + offset + 4, type, 4)) {
            if (box_size)
                *box_size = length;
            return bytes + offset;
        }
        offset += length;
    }
    return NULL;
}

static void make_path(char output[1024], const char *directory, const char *name) {
    int count = snprintf(output, 1024, "%s/%s", directory, name);
    assert(count > 0 && count < 1024);
}

static void check_indexes(const char *path, uint64_t video_ticks, uint32_t video_count,
                          uint32_t audio_count) {
    TestFile file = read_file(path);
    assert(file.size > 48 && read_u32(file.bytes) == 32);
    assert(!memcmp(file.bytes + 4, "ftyp", 4));
    assert(read_u32(file.bytes + 32) == 1);
    assert(!memcmp(file.bytes + 36, "mdat", 4));
    uint64_t media_size = read_u64(file.bytes + 40);
    assert(media_size >= 16 && media_size < file.size - 32);
    size_t movie_offset = 32 + (size_t)media_size;
    const uint8_t *movie = file.bytes + movie_offset;
    size_t movie_size = file.size - movie_offset;
    assert(read_u32(movie) == movie_size && !memcmp(movie + 4, "moov", 4));
    const uint8_t *header = find_box(movie + 8, movie_size - 8, "mvhd", NULL);
    assert(header && header[8] == 1 && read_u32(header + 28) == 1000000);
    const uint8_t *tracks = header + read_u32(header);
    size_t remaining = movie_size - (size_t)(tracks - movie);
    for (unsigned track_index = video_count ? 0 : 1; track_index < 2; ++track_index) {
        size_t track_size;
        const uint8_t *track = find_box(tracks, remaining, "trak", &track_size);
        assert(track);
        size_t media_length;
        const uint8_t *media =
            find_box(track + 8, track_size - 8, "mdia", &media_length);
        assert(media);
        const uint8_t *media_header =
            find_box(media + 8, media_length - 8, "mdhd", NULL);
        assert(media_header && media_header[8] == 1);
        assert(read_u64(media_header + 32) ==
               (track_index ? audio_count : video_ticks));
        size_t information_size;
        const uint8_t *information =
            find_box(media + 8, media_length - 8, "minf", &information_size);
        assert(information);
        size_t table_size;
        const uint8_t *table =
            find_box(information + 8, information_size - 8, "stbl", &table_size);
        assert(table);
        const uint8_t *sizes = find_box(table + 8, table_size - 8, "stsz", NULL);
        const uint8_t *offsets = find_box(table + 8, table_size - 8, "co64", NULL);
        const uint8_t *times = find_box(table + 8, table_size - 8, "stts", NULL);
        assert(sizes && offsets && times);
        const uint8_t *sync = find_box(table + 8, table_size - 8, "stss", NULL);
        if (!track_index && sync) {
            uint32_t sync_count = read_u32(sync + 12);
            assert(sync_count > 0 && sync_count <= video_count);
            uint32_t previous = 0;
            for (uint32_t i = 0; i < sync_count; ++i) {
                uint32_t sample = read_u32(sync + 16 + (size_t)i * 4);
                assert(sample > previous && sample <= video_count);
                assert(i != 0 || sample == 1);
                previous = sample;
            }
        }
        assert(read_u32(sizes + 16) == (track_index ? audio_count : video_count));
        assert(read_u32(sizes + 12) == (track_index ? 8 : 0));
        uint32_t chunks = read_u32(offsets + 12);
        for (uint32_t i = 0; i < chunks; ++i) {
            uint64_t offset = read_u64(offsets + 16 + (size_t)i * 8);
            assert(offset >= 48 && offset < movie_offset);
        }
        if (!track_index && video_count == 4) {
            assert(read_u32(times + 12) == 3);
            assert(read_u32(times + 16) == 2 && read_u32(times + 20) == 1001);
            assert(read_u32(times + 24) == 1 && read_u32(times + 28) == 2002);
            assert(read_u32(times + 32) == 1 && read_u32(times + 36) == 3003);
        }
        tracks = track + track_size;
        remaining = movie_size - (size_t)(tracks - movie);
    }
    free(file.bytes);
}

static void fill_frame(uint8_t *rgba, unsigned width, unsigned height, size_t stride,
                       unsigned frame) {
    memset(rgba, 0xa5, stride * height);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint8_t *pixel = rgba + (size_t)y * stride + (size_t)x * 4;
            pixel[0] = (uint8_t)(x * 67 + y * 31 + frame * 17);
            pixel[1] = (uint8_t)(x * 5 + y * 97 + frame * 83);
            pixel[2] = (uint8_t)(x * 113 + y * 11 + frame * 47);
            if (frame == 1)
                memset(pixel, 255, 3);
            else if (frame == 2)
                memset(pixel, 0, 3);
            else if (frame == 3) {
                for (unsigned component = 0; component < 3; ++component)
                    pixel[component] = (uint8_t)((x + y + component) % 4);
            }
            pixel[3] = (uint8_t)(x + y + frame);
        }
    }
}

static void write_rgb_reference(FILE *file, const uint8_t *rgba, unsigned width,
                                unsigned height, size_t stride) {
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x)
            assert(fwrite(rgba + (size_t)y * stride + (size_t)x * 4, 1, 3, file) == 3);
    }
}

static void test_roundtrip_fixture(const char *directory) {
    char path[1024];
    char rgb_path[1024];
    char audio_path[1024];
    make_path(path, directory, "capture-roundtrip.mp4");
    make_path(rgb_path, directory, "capture-roundtrip.rgb");
    make_path(audio_path, directory, "capture-roundtrip.f32");
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open(path, 19, 17, 60000, 48000);
    assert(writer && !cc_capture_writer_error(writer)[0]);
    assert(!cc_capture_writer_open(path, 19, 17, 60000, 48000));
    FILE *rgb = fopen(rgb_path, "wb");
    FILE *audio = fopen(audio_path, "wb");
    assert(rgb && audio);
    uint8_t rgba[17 * 96];
    float stereo[256];
    const uint32_t audio_words[8] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000,
                                     0x00800000, 0x00000001, 0x3eaaaaab, 0xbeaaaaab};
    for (size_t i = 0; i < sizeof(stereo) / sizeof(stereo[0]); ++i)
        memcpy(stereo + i, audio_words + i % 8, 4);
    const uint32_t durations[4] = {1001, 1001, 2002, 3003};
    const uint32_t audio_lengths[4] = {1, 128, 37, 128};
    uint32_t audio_count = 0;
    for (unsigned frame = 0; frame < 4; ++frame) {
        fill_frame(rgba, 19, 17, 96, frame);
        assert(cc_capture_writer_video(writer, rgba, 96, durations[frame]));
        write_rgb_reference(rgb, rgba, 19, 17, 96);
        assert(cc_capture_writer_audio(writer, stereo, audio_lengths[frame]));
        assert(fwrite(stereo, 8, audio_lengths[frame], audio) == audio_lengths[frame]);
        audio_count += audio_lengths[frame];
    }
    assert(cc_capture_writer_audio(writer, NULL, 0));
    assert(cc_capture_writer_close(writer));
    assert(fclose(rgb) == 0 && fclose(audio) == 0);
    check_indexes(path, 7007, 4, audio_count);
}

static void test_video_codec_fixture(const char *directory) {
    char path[1024];
    char rgb_path[1024];
    make_path(path, directory, "capture-codec.mp4");
    make_path(rgb_path, directory, "capture-codec.rgb");
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open(path, 64, 64, 60000, 48000);
    FILE *rgb = fopen(rgb_path, "wb");
    assert(writer && rgb);
    uint8_t rgba[64 * 64 * 4];
    const uint32_t durations[4] = {1001, 1001, 2002, 3003};
    for (unsigned frame = 0; frame < 4; ++frame) {
        for (unsigned y = 0; y < 64; ++y) {
            for (unsigned x = 0; x < 64; ++x) {
                uint8_t *pixel = rgba + ((size_t)y * 64 + x) * 4;
                pixel[0] = (uint8_t)(x * 4);
                pixel[1] = (uint8_t)(y * 4);
                pixel[2] = (uint8_t)((x + y) * 2);
                if (x >= frame * 8 && x < frame * 8 + 16 && y >= 24 && y < 40)
                    memset(pixel, 255, 3);
                pixel[3] = 255;
            }
        }
        assert(cc_capture_writer_video(writer, rgba, 64 * 4, durations[frame]));
        write_rgb_reference(rgb, rgba, 64, 64, 64 * 4);
    }
    assert(cc_capture_writer_close(writer) && fclose(rgb) == 0);
    check_indexes(path, 7007, 4, 0);
}

static void test_rejected_append(const char *directory) {
    char path[1024];
    make_path(path, directory, "capture-prefix.mp4");
    const unsigned modes = 5;
    for (unsigned mode = 0; mode < modes; ++mode) {
        remove(path);
        CcCaptureWriter *writer = cc_capture_writer_open(path, 1, 1, INT32_MAX, 32000);
        assert(writer);
        uint8_t rgba[4] = {17, 33, 65, 255};
        float stereo[2] = {0.25f, -0.25f};
        assert(cc_capture_writer_video(writer, rgba, 4, UINT32_MAX));
        assert(cc_capture_writer_video(writer, rgba, 4, UINT32_MAX));
        assert(cc_capture_writer_audio(writer, stereo, 1));
        if (mode == 0)
            assert(!cc_capture_writer_video(writer, rgba, 4, 0));
        else if (mode == 1)
            assert(!cc_capture_writer_video(writer, rgba, 3, 1));
        else if (mode == 2)
            assert(!cc_capture_writer_video(writer, NULL, 4, 1));
        else if (mode == 3)
            assert(!cc_capture_writer_audio(writer, NULL, 1));
        else
            assert(!cc_capture_writer_audio(writer, stereo, SIZE_MAX));
        assert(cc_capture_writer_error(writer)[0]);
        assert(!cc_capture_writer_audio(writer, stereo, 1));
        assert(!cc_capture_writer_video(writer, rgba, 4, 1));
        assert(!cc_capture_writer_close(writer));
        check_indexes(path, (uint64_t)UINT32_MAX * 2, 2, 1);
    }
    remove(path);
}

static void test_validation_and_empty(const char *directory) {
    char path[1024];
    make_path(path, directory, "capture-empty.mp4");
    remove(path);
    assert(!cc_capture_writer_open(NULL, 1, 1, 60, 48000));
    assert(!cc_capture_writer_open("", 1, 1, 60, 48000));
    assert(!cc_capture_writer_open(path, 0, 1, 60, 48000));
    assert(!cc_capture_writer_open(path, 1, 4097, 60, 48000));
    assert(!cc_capture_writer_open(path, 1, 1, 0, 48000));
    assert(!cc_capture_writer_open(path, 1, 1, UINT32_MAX, 48000));
    assert(!cc_capture_writer_open(path, 1, 1, 60, 0));
    assert(!cc_capture_writer_open(path, 1, 1, 60, 65536));
    assert(!cc_capture_writer_audio(NULL, NULL, 0));
    assert(!cc_capture_writer_video(NULL, NULL, 0, 1));
    assert(!cc_capture_writer_error(NULL)[0]);
    assert(cc_capture_writer_close(NULL));
    CcCaptureWriter *writer = cc_capture_writer_open(path, 1, 1, 60, 48000);
    assert(writer && cc_capture_writer_close(writer));
    check_indexes(path, 0, 0, 0);
    remove(path);
}

int main(int argc, char **argv) {
    assert(argc == 2 && argv[1][0]);
    test_validation_and_empty(argv[1]);
    test_roundtrip_fixture(argv[1]);
    test_video_codec_fixture(argv[1]);
    test_rejected_append(argv[1]);
    return 0;
}
