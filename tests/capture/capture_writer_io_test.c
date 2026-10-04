#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int writes_before_failure = -1;
static bool fail_next_reallocation;
static bool zero_failed_write;

static size_t test_write(const void *bytes, size_t size, size_t count, FILE *file) {
    if (writes_before_failure < 0)
        return fwrite(bytes, size, count, file);
    if (writes_before_failure-- > 0)
        return fwrite(bytes, size, count, file);
    writes_before_failure = -1;
    return fwrite(bytes, size, zero_failed_write ? 0 : count / 2, file);
}

static void *test_reallocate(void *pointer, size_t size) {
    if (fail_next_reallocation) {
        fail_next_reallocation = false;
        return NULL;
    }
    return realloc(pointer, size);
}

/* Inject transient I/O and allocation failures into the actual writer module. */
#define fwrite test_write
#define realloc test_reallocate
#include "../../src/capture/capture_writer.c"
#undef realloc
#undef fwrite

static uint64_t read_extended_size(FILE *file) {
    uint8_t bytes[8];
    assert(fseek(file, 40, SEEK_SET) == 0 && fread(bytes, 1, 8, file) == 8);
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value = value << 8 | bytes[i];
    return value;
}

static void verify_completed_prefix(const char *path, uint64_t media_end) {
    FILE *file = fopen(path, "rb");
    assert(file && read_extended_size(file) == media_end - 32);
    assert(fseek(file, (long)media_end, SEEK_SET) == 0);
    uint8_t header[8];
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    uint32_t length = (uint32_t)header[0] << 24 | (uint32_t)header[1] << 16 |
                      (uint32_t)header[2] << 8 | header[3];
    assert(length > 8 && !memcmp(header + 4, "moov", 4));
    assert(fseek(file, 0, SEEK_END) == 0 &&
           (uint64_t)ftell(file) == media_end + length);
    assert(fclose(file) == 0);
}

static void test_header_failure(const char *path) {
    for (int write_index = 0; write_index < 9; ++write_index) {
        remove(path);
        writes_before_failure = write_index;
        zero_failed_write = write_index % 2 == 0;
        CcCaptureWriter *writer = cc_capture_writer_open(path, 1, 1, 60, 48000);
        assert(!writer);
        errno = 0;
        FILE *file = fopen(path, "rb");
        assert(!file && errno == ENOENT);
    }
    FILE *file = fopen(path, "wb");
    assert(file && fputs("Existing recording", file) >= 0 && fclose(file) == 0);
    writes_before_failure = 0;
    assert(!cc_capture_writer_open(path, 1, 1, 60, 48000));
    assert(writes_before_failure == 0);
    writes_before_failure = -1;
    file = fopen(path, "rb");
    char contents[32] = {0};
    assert(file && fread(contents, 1, sizeof(contents), file) == 18);
    assert(!strcmp(contents, "Existing recording") && fclose(file) == 0);
    remove(path);
}

static void test_encoded_audio_failure(const char *path, bool during_finish) {
    if (!cc_capture_audio_mode_supported(CC_CAPTURE_AUDIO_WEB))
        return;
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open_with_audio(
        path, 64, 64, 60000, 48000, CC_CAPTURE_AUDIO_WEB);
    assert(writer);
    float stereo[8192 * 2] = {0};
    assert(cc_capture_writer_audio(writer, stereo, 8192));
    assert(writer->audio.count &&
           writer->audio.duration > writer->audio_config.priming_frames);
    uint64_t completed = writer->complete_position;
    size_t packets = writer->audio.count;
    writes_before_failure = 0;
    zero_failed_write = false;
    if (!during_finish) {
        assert(!cc_capture_writer_audio(writer, stereo, 8192));
        assert(writer->audio.count == packets &&
               writer->complete_position == completed);
    }
    assert(!cc_capture_writer_close(writer));
    assert(writes_before_failure == -1);
    verify_completed_prefix(path, completed);
}

static void test_append_failure(const char *path, unsigned mode) {
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open(path, 17, 19, 60000, 48000);
    assert(writer);
    uint8_t rgba[17 * 19 * 4] = {0};
    float stereo[2] = {0.125f, -0.125f};
    assert(cc_capture_writer_video(writer, rgba, 17 * 4, 1001));
    assert(cc_capture_writer_audio(writer, stereo, 1));
    uint64_t completed = writer->complete_position;
    writes_before_failure = 0;
    zero_failed_write = mode == 1;
    bool result = mode == 2 ? cc_capture_writer_audio(writer, stereo, 1)
                            : cc_capture_writer_video(writer, rgba, 17 * 4, 1001);
    assert(!result && writer->video.count == 1 && writer->audio.count == 1);
    assert(writer->complete_position == completed &&
           cc_capture_writer_error(writer)[0]);
    assert(!cc_capture_writer_close(writer));
    verify_completed_prefix(path, completed);
}

static void test_index_allocation_failure(const char *path, bool video) {
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open(path, 1, 1, 60, 48000);
    assert(writer);
    uint8_t rgba[4] = {0};
    float stereo[2] = {0};
    fail_next_reallocation = true;
    bool result = video ? cc_capture_writer_video(writer, rgba, 4, 1)
                        : cc_capture_writer_audio(writer, stereo, 1);
    assert(!result && !writer->video.count && !writer->audio.count);
    assert(!cc_capture_writer_close(writer));
    verify_completed_prefix(path, 48);
}

static void test_large_file_offsets(const char *path) {
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open(path, 1, 1, 60, 48000);
    assert(writer);
    uint64_t sparse_offset = UINT64_C(0x100000080);
    /* A sparse gap exercises 64-bit offsets without writing gigabytes of pixels. */
    assert(seek_file(writer->file, sparse_offset));
    writer->position = sparse_offset;
    uint8_t rgba[4] = {3, 2, 1, 255};
    assert(cc_capture_writer_video(writer, rgba, 4, 1));
    uint64_t completed = writer->complete_position;
    assert(cc_capture_writer_close(writer));

    FILE *file = fopen(path, "rb");
    assert(file && read_extended_size(file) == completed - 32);
    assert(seek_file(file, completed));
    uint8_t header[8];
    assert(fread(header, 1, 8, file) == 8 && !memcmp(header + 4, "moov", 4));
    uint32_t length = (uint32_t)header[0] << 24 | (uint32_t)header[1] << 16 |
                      (uint32_t)header[2] << 8 | header[3];
    assert(length < 4096);
    uint8_t movie[4096];
    memcpy(movie, header, sizeof(header));
    assert(fread(movie + 8, 1, length - 8, file) == length - 8);
    bool found_offset = false;
    for (uint32_t i = 4; i + 20 <= length; ++i) {
        if (memcmp(movie + i, "co64", 4) || movie[i + 11] != 1)
            continue;
        uint64_t offset = 0;
        for (unsigned byte = 0; byte < 8; ++byte)
            offset = offset << 8 | movie[i + 12 + byte];
        assert(offset == sparse_offset);
        found_offset = true;
    }
    assert(found_offset && fclose(file) == 0);
    remove(path);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/capture-io-prefix.mp4", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(path));
    test_header_failure(path);
    test_encoded_audio_failure(path, false);
    test_encoded_audio_failure(path, true);
    for (unsigned mode = 0; mode < 3; ++mode)
        test_append_failure(path, mode);
    test_index_allocation_failure(path, true);
    test_index_allocation_failure(path, false);
    test_large_file_offsets(path);
    remove(path);
    return 0;
}
