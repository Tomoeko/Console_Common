#define _POSIX_C_SOURCE 200809L

#include "console_common/capture/audio_buffer.h"
#include "console_common/capture/recording.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t header;
} TestBox;

typedef struct {
    uint64_t video_ticks;
    uint32_t video_frames;
    uint32_t audio_frames;
    unsigned video_width;
    unsigned video_height;
    unsigned audio_rate;
    uint32_t *audio_words;
} TestMovie;

static unsigned read_u16(const uint8_t *bytes) {
    return (unsigned)bytes[0] << 8 | bytes[1];
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint64_t read_u64(const uint8_t *bytes) {
    return (uint64_t)read_u32(bytes) << 32 | read_u32(bytes + 4);
}

static TestBox find_box(const uint8_t *bytes, size_t size, const char type[4]) {
    size_t offset = 0;
    while (size - offset >= 8) {
        uint64_t length = read_u32(bytes + offset);
        size_t header = 8;
        if (length == 1) {
            assert(size - offset >= 16);
            length = read_u64(bytes + offset + 8);
            header = 16;
        }
        assert(length >= header && length <= size - offset);
        if (!memcmp(bytes + offset + 4, type, 4))
            return (TestBox){bytes + offset, (size_t)length, header};
        offset += (size_t)length;
    }
    return (TestBox){0};
}

static TestBox child_box(TestBox parent, const char type[4]) {
    assert(parent.bytes && parent.size >= parent.header);
    TestBox child =
        find_box(parent.bytes + parent.header, parent.size - parent.header, type);
    assert(child.bytes);
    return child;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

static void read_audio_chunks(TestMovie *movie, TestBox table, const uint8_t *file,
                              size_t file_size) {
    TestBox sizes = child_box(table, "stsz");
    TestBox chunks = child_box(table, "co64");
    TestBox layout = child_box(table, "stsc");
    assert(sizes.size >= 20 && read_u32(sizes.bytes + 12) == 8);
    movie->audio_frames = read_u32(sizes.bytes + 16);
    size_t word_count = (size_t)movie->audio_frames * 2;
    assert(word_count <= SIZE_MAX / sizeof(uint32_t));
    movie->audio_words = calloc(word_count ? word_count : 1, sizeof(uint32_t));
    assert(movie->audio_words && chunks.size >= 16 && layout.size >= 16);
    unsigned chunk_count = read_u32(chunks.bytes + 12);
    unsigned layout_count = read_u32(layout.bytes + 12);
    assert(chunk_count <= (chunks.size - 16) / 8);
    assert(layout_count <= (layout.size - 16) / 12);
    unsigned entry = 0;
    size_t completed = 0;
    for (unsigned chunk = 1; chunk <= chunk_count; ++chunk) {
        assert(layout_count && read_u32(layout.bytes + 16) == 1);
        while (entry + 1 < layout_count &&
               read_u32(layout.bytes + 16 + (size_t)(entry + 1) * 12) <= chunk)
            ++entry;
        unsigned count = read_u32(layout.bytes + 20 + (size_t)entry * 12);
        assert(read_u32(layout.bytes + 24 + (size_t)entry * 12) == 1);
        uint64_t offset = read_u64(chunks.bytes + 16 + (size_t)(chunk - 1) * 8);
        assert(offset <= file_size && count <= (file_size - (size_t)offset) / 8);
        assert(completed <= movie->audio_frames &&
               count <= movie->audio_frames - completed);
        for (size_t word = 0; word < (size_t)count * 2; ++word) {
            const uint8_t *bytes = file + (size_t)offset + word * 4;
            movie->audio_words[completed * 2 + word] =
                (uint32_t)bytes[3] << 24 | (uint32_t)bytes[2] << 16 |
                (uint32_t)bytes[1] << 8 | bytes[0];
        }
        completed += count;
    }
    assert(completed == movie->audio_frames);
}

static void check_jpeg_dimensions(const TestMovie *movie, TestBox table,
                                  const uint8_t *file, size_t file_size) {
    TestBox chunks = child_box(table, "co64");
    TestBox sizes = child_box(table, "stsz");
    assert(chunks.size >= 24 && sizes.size >= 24);
    uint64_t offset = read_u64(chunks.bytes + 16);
    uint32_t size = read_u32(sizes.bytes + 20);
    assert(offset <= file_size && size <= file_size - (size_t)offset);
    const uint8_t *sample = file + (size_t)offset;
    assert(size >= 4 && sample[0] == 0xff && sample[1] == 0xd8);
    size_t position = 2;
    while (position + 4 <= size) {
        assert(sample[position] == 0xff);
        unsigned marker = sample[position + 1];
        size_t length = read_u16(sample + position + 2);
        assert(length >= 2 && length <= size - position - 2);
        if (marker == 0xc0) {
            assert(length >= 8 && sample[position + 4] == 8);
            assert(read_u16(sample + position + 5) == movie->video_height);
            assert(read_u16(sample + position + 7) == movie->video_width);
            return;
        }
        assert(marker != 0xda && marker != 0xd9);
        position += length + 2;
    }
    assert(false);
}

static TestMovie read_movie(const char *path) {
    size_t file_size;
    uint8_t *file = read_file(path, &file_size);
    TestBox movie_box = find_box(file, file_size, "moov");
    assert(movie_box.bytes);
    TestMovie movie = {0};
    size_t offset = movie_box.header;
    unsigned seen = 0;
    while (offset < movie_box.size) {
        TestBox track =
            find_box(movie_box.bytes + offset, movie_box.size - offset, "trak");
        if (!track.bytes)
            break;
        TestBox media = child_box(track, "mdia");
        TestBox header = child_box(media, "mdhd");
        TestBox handler = child_box(media, "hdlr");
        TestBox table = child_box(child_box(media, "minf"), "stbl");
        assert(header.size >= 40 && header.bytes[8] == 1 && handler.size >= 20);
        if (!memcmp(handler.bytes + 16, "vide", 4)) {
            assert(!(seen & 1) && read_u32(header.bytes + 28) == 1000000);
            seen |= 1;
            movie.video_ticks = read_u64(header.bytes + 32);
            TestBox sizes = child_box(table, "stsz");
            assert(sizes.size >= 20 && read_u32(sizes.bytes + 12) == 0);
            movie.video_frames = read_u32(sizes.bytes + 16);
            TestBox description = child_box(table, "stsd");
            assert(description.size >= 16 && read_u32(description.bytes + 12) == 1);
            TestBox entry =
                find_box(description.bytes + 16, description.size - 16, "mp4v");
            bool jpeg = entry.bytes != NULL;
            if (!jpeg)
                entry = find_box(description.bytes + 16, description.size - 16, "avc1");
            assert(entry.bytes && entry.size >= 36);
            movie.video_width = read_u16(entry.bytes + 32);
            movie.video_height = read_u16(entry.bytes + 34);
            TestBox track_header = child_box(track, "tkhd");
            assert(track_header.size >= 104 && track_header.bytes[8] == 1);
            assert(read_u32(track_header.bytes + 96) == movie.video_width << 16);
            assert(read_u32(track_header.bytes + 100) == movie.video_height << 16);
            if (jpeg)
                check_jpeg_dimensions(&movie, table, file, file_size);
            TestBox times = child_box(table, "stts");
            assert(times.size >= 16);
            unsigned runs = read_u32(times.bytes + 12);
            assert(runs <= (times.size - 16) / 8);
            uint64_t duration = 0;
            uint64_t frames = 0;
            for (unsigned run = 0; run < runs; ++run) {
                unsigned count = read_u32(times.bytes + 16 + (size_t)run * 8);
                unsigned ticks = read_u32(times.bytes + 20 + (size_t)run * 8);
                assert(count && ticks);
                frames += count;
                duration += (uint64_t)count * ticks;
            }
            assert(frames == movie.video_frames && duration == movie.video_ticks);
        } else {
            assert(!(seen & 2) && !memcmp(handler.bytes + 16, "soun", 4));
            seen |= 2;
            movie.audio_rate = read_u32(header.bytes + 28);
            assert(movie.audio_rate >= 8000 && movie.audio_rate <= 65535);
            read_audio_chunks(&movie, table, file, file_size);
            assert(read_u64(header.bytes + 32) == movie.audio_frames);
        }
        offset = (size_t)(track.bytes - movie_box.bytes) + track.size;
    }
    assert(seen == 3);
    free(file);
    return movie;
}

static void check_audio(const TestMovie *movie, const float *expected, size_t frames) {
    assert(movie->audio_frames == frames);
    for (size_t word = 0; word < frames * 2; ++word) {
        uint32_t expected_word;
        memcpy(&expected_word, expected + word, sizeof(expected_word));
        assert(movie->audio_words[word] == expected_word);
    }
}

struct CcPlatform {
    uint8_t pixels[19 * 17 * 4];
    int width;
    int height;
    size_t stride;
    unsigned begins;
    unsigned ends;
    bool active;
    bool ready;
    bool reject_begin;
};

typedef struct {
    CcAudioBuffer *buffer;
    unsigned begins;
    unsigned ends;
    unsigned reads;
    size_t capacity;
    bool reject_begin;
    bool invalid_count;
    bool endless;
} TestAudio;

bool cc_platform_capture_begin(CcPlatform *platform, CcFramebuffer *frame) {
    assert(platform && frame);
    ++platform->begins;
    if (platform->reject_begin || platform->active)
        return false;
    platform->active = true;
    *frame = (CcFramebuffer){.width = platform->width,
                             .height = platform->height,
                             .stride = platform->stride};
    return true;
}

bool cc_platform_capture_frame(CcPlatform *platform, CcFramebuffer *frame) {
    assert(platform && frame);
    if (!platform->active || !platform->ready)
        return false;
    *frame = (CcFramebuffer){.rgba = platform->pixels,
                             .width = platform->width,
                             .height = platform->height,
                             .stride = platform->stride};
    return true;
}

void cc_platform_capture_end(CcPlatform *platform) {
    assert(platform && platform->active);
    platform->active = false;
    ++platform->ends;
}

static CcPlatform test_platform(void) {
    return (CcPlatform){.pixels = {40, 80, 120, 255, 160, 120, 80, 255, 80, 160, 120,
                                   255, 120, 80, 160, 255},
                        .width = 2,
                        .height = 2,
                        .stride = 8,
                        .ready = true};
}

static bool begin_audio(void *context, size_t capacity_frames) {
    TestAudio *audio = context;
    assert(!audio->buffer);
    ++audio->begins;
    audio->capacity = capacity_frames;
    if (audio->reject_begin)
        return false;
    audio->buffer = cc_audio_buffer_create(capacity_frames);
    return audio->buffer != NULL;
}

static size_t read_audio(void *context, float *stereo, size_t capacity_frames) {
    TestAudio *audio = context;
    assert(audio->buffer);
    ++audio->reads;
    if (audio->invalid_count)
        return capacity_frames + 1;
    if (audio->endless) {
        stereo[0] = 0;
        stereo[1] = 0;
        return 1;
    }
    return cc_audio_buffer_read(audio->buffer, stereo, capacity_frames, NULL);
}

static bool failed_audio(void *context) {
    TestAudio *audio = context;
    assert(audio->buffer);
    return cc_audio_buffer_failed(audio->buffer);
}

static void end_audio(void *context) {
    TestAudio *audio = context;
    assert(audio->buffer);
    ++audio->ends;
    cc_audio_buffer_destroy(audio->buffer);
    audio->buffer = NULL;
}

static CcRecordingOptions test_options(CcPlatform *platform, TestAudio *audio) {
    CcRecordingOptions options = {.platform = platform,
                                  .sample_rate = 48000,
                                  .video_rate = 60,
                                  .filename_prefix = "Capture_Test"};
    if (audio) {
        options.audio = (CcRecordingAudioSource){.context = audio,
                                                 .begin = begin_audio,
                                                 .read = read_audio,
                                                 .failed = failed_audio,
                                                 .end = end_audio};
    }
    return options;
}

static void make_path(char path[1024], const char *directory, const char *name) {
    int count = snprintf(path, 1024, "%s/%s", directory, name);
    assert(count > 0 && count < 1024);
}

static void fill_audio(float *stereo, size_t frames, unsigned phase) {
    for (size_t frame = 0; frame < frames; ++frame) {
        stereo[frame * 2] = (float)((frame + phase) % 257) / 512 - 0.25f;
        stereo[frame * 2 + 1] = (float)((frame * 7 + phase) % 257) / 512 - 0.25f;
    }
    stereo[1] = -0.0f;
}

static void check_cleanup(const CcPlatform *platform, const TestAudio *audio) {
    assert(!platform->active && platform->begins == platform->ends);
    if (audio)
        assert(!audio->buffer && audio->begins == audio->ends);
}

static void test_options_and_contract(const char *directory) {
    char path[1024];
    make_path(path, directory, "options.mp4");
    remove(path);
    CcPlatform platform = test_platform();
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    assert(!cc_recording_open_path(NULL, path));
    assert(!cc_recording_open_path(&options, NULL));
    assert(!cc_recording_open_path(&options, ""));
    CcRecordingOptions invalid = options;
    invalid.platform = NULL;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.sample_rate = 0;
    assert(!cc_recording_open_path(&invalid, path));
    invalid.sample_rate = 65536;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.video_rate = 59;
    assert(!cc_recording_open_path(&invalid, path));
    const char *prefixes[] = {"", "../escape", "slash/name", "space name", "."};
    for (size_t index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        invalid = options;
        invalid.filename_prefix = prefixes[index];
        assert(!cc_recording_open_path(&invalid, path));
    }
    char long_prefix[66];
    memset(long_prefix, 'x', sizeof(long_prefix) - 1);
    long_prefix[sizeof(long_prefix) - 1] = 0;
    invalid = options;
    invalid.filename_prefix = long_prefix;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.audio.read = NULL;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.audio.failed = NULL;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.audio.end = NULL;
    assert(!cc_recording_open_path(&invalid, path));
    invalid = options;
    invalid.audio.begin = NULL;
    assert(!cc_recording_open_path(&invalid, path));
    assert(!platform.begins && !audio.begins);

    platform.reject_begin = true;
    assert(!cc_recording_open_path(&options, path));
    assert(!platform.active && platform.begins == 1 && platform.ends == 0);
    platform = test_platform();
    platform.stride = 1;
    assert(!cc_recording_open_path(&options, path));
    check_cleanup(&platform, &audio);
    platform = test_platform();
    audio.reject_begin = true;
    assert(!cc_recording_open_path(&options, path));
    assert(!audio.buffer && audio.begins == 1 && audio.ends == 0);
    check_cleanup(&platform, NULL);
    audio = (TestAudio){0};
    options = test_options(&platform, NULL);
    long_prefix[64] = 0;
    options.filename_prefix = long_prefix;
    CcRecording *recording = cc_recording_open_path(&options, path);
    assert(recording);
    assert(cc_recording_frame(recording, 1000));
    assert(cc_recording_close(recording, 1000.03125));
    check_cleanup(&platform, NULL);
    TestMovie movie = read_movie(path);
    assert(movie.audio_rate == 48000 && movie.audio_frames == 1500);
    free(movie.audio_words);
    assert(cc_recording_frame(NULL, NAN) && cc_recording_pump(NULL, NAN));
    assert(cc_recording_audio_start(NULL, NAN) && cc_recording_audio_stop(NULL, NAN));
    assert(cc_recording_close(NULL, NAN));
}

static void test_exact_delay_prefill_and_stop(const char *directory) {
    char path[1024];
    make_path(path, directory, "prefill.mp4");
    remove(path);
    CcPlatform platform = test_platform();
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    CcRecording *recording = cc_recording_open_path(&options, path);
    assert(recording && audio.capacity == 96000);
    assert(!strcmp(cc_recording_path(recording), path));
    assert(!cc_recording_audio_start(recording, 1000));
    assert(cc_recording_frame(recording, 1000));
    float first[1536 * 2];
    float second[1536 * 2];
    fill_audio(first, 1536, 3);
    fill_audio(second, 1536, 41);
    cc_audio_buffer_write(audio.buffer, first, 1536);
    assert(cc_recording_pump(recording, 1000.125));
    assert(cc_recording_audio_start(recording, 1000.125));
    assert(!cc_recording_audio_start(recording, 1000.125));
    assert(cc_recording_pump(recording, 1000.140625));
    assert(cc_recording_audio_stop(recording, 1000.15625));
    assert(cc_recording_frame(recording, 1000.15625));
    cc_audio_buffer_write(audio.buffer, second, 1536);
    assert(cc_recording_pump(recording, 1000.1875));
    assert(cc_recording_audio_start(recording, 1000.1875));
    assert(cc_recording_pump(recording, 1000.203125));
    assert(cc_recording_audio_stop(recording, 1000.21875));
    assert(cc_recording_frame(recording, 1000.21875));
    assert(cc_recording_close(recording, 1000.25));
    check_cleanup(&platform, &audio);
    TestMovie movie = read_movie(path);
    assert(movie.video_ticks == 250000 && movie.video_frames == 3);
    float expected[12000 * 2] = {0};
    memcpy(expected + 6000 * 2, first, 1500 * 2 * sizeof(float));
    memcpy(expected + 9000 * 2, second, 1500 * 2 * sizeof(float));
    check_audio(&movie, expected, 12000);
    free(movie.audio_words);
}

static void test_variable_rate_and_silent_pause(const char *directory) {
    char path[1024];
    make_path(path, directory, "sample-rate.mp4");
    remove(path);
    CcPlatform platform = test_platform();
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    options.sample_rate = 44100;
    CcRecording *recording = cc_recording_open_path(&options, path);
    assert(recording && audio.capacity == 88200);
    assert(cc_recording_frame(recording, 1000));
    float prefill[1536 * 2];
    fill_audio(prefill, 1536, 7);
    cc_audio_buffer_write(audio.buffer, prefill, 1536);
    assert(cc_recording_audio_start(recording, 1000));
    assert(cc_recording_pump(recording, 1000.015625));
    assert(cc_recording_audio_stop(recording, 1000.03125));
    assert(cc_recording_close(recording, 1000.03125));
    check_cleanup(&platform, &audio);
    TestMovie movie = read_movie(path);
    assert(movie.video_ticks == 31250 && movie.audio_rate == 44100);
    check_audio(&movie, prefill, 1378);
    free(movie.audio_words);

    make_path(path, directory, "silent-pause.mp4");
    remove(path);
    options = test_options(&platform, NULL);
    options.video_rate = 50;
    recording = cc_recording_open_path(&options, path);
    assert(recording);
    assert(cc_recording_frame(recording, 1000));
    assert(cc_recording_frame(recording, 1000));
    assert(cc_recording_audio_start(recording, 1000));
    assert(cc_recording_pump(recording, 1000.125));
    assert(cc_recording_frame(recording, 1000.125));
    assert(cc_recording_pump(recording, 1000.375));
    assert(cc_recording_frame(recording, 1000.375));
    assert(cc_recording_close(recording, 1000.5));
    check_cleanup(&platform, NULL);
    movie = read_movie(path);
    assert(movie.video_ticks == 500000 && movie.video_frames == 3);
    assert(movie.audio_frames == 24000);
    for (size_t word = 0; word < 48000; ++word)
        assert(movie.audio_words[word] == 0);
    free(movie.audio_words);
}

static TestMovie record_size_and_audio(const char *directory, bool half_size) {
    char path[1024];
    make_path(path, directory, half_size ? "half-audio.mp4" : "full-audio.mp4");
    remove(path);
    CcPlatform platform = test_platform();
    platform.width = 5;
    platform.height = 3;
    platform.stride = 20;
    for (size_t sample = 0; sample < platform.stride * (size_t)platform.height;
         ++sample)
        platform.pixels[sample] = (uint8_t)(sample * 31);
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    options.half_size = half_size;
    CcRecording *recording = cc_recording_open_path(&options, path);
    assert(recording && cc_recording_frame(recording, 1000));
    assert(cc_recording_pump(recording, 1000.125));
    float prefill[1536 * 2];
    fill_audio(prefill, 1536, 17);
    cc_audio_buffer_write(audio.buffer, prefill, 1536);
    assert(cc_recording_audio_start(recording, 1000.125));
    assert(cc_recording_frame(recording, 1000.125));
    assert(cc_recording_audio_stop(recording, 1000.15625));
    assert(cc_recording_close(recording, 1000.1875));
    check_cleanup(&platform, &audio);
    TestMovie movie = read_movie(path);
    assert(movie.video_width == (half_size ? 3U : 5U));
    assert(movie.video_height == (half_size ? 2U : 3U));
    assert(movie.video_ticks == 187500 && movie.video_frames == 2);
    assert(movie.audio_rate == 48000);
    float expected[9000 * 2] = {0};
    memcpy(expected + 6000 * 2, prefill, 1500 * 2 * sizeof(float));
    check_audio(&movie, expected, 9000);
    return movie;
}

static void test_half_size_metadata_and_audio(const char *directory) {
    TestMovie full = record_size_and_audio(directory, false);
    TestMovie half = record_size_and_audio(directory, true);
    assert(full.audio_frames == half.audio_frames &&
           full.audio_rate == half.audio_rate);
    assert(!memcmp(full.audio_words, half.audio_words,
                   (size_t)full.audio_frames * 2 * sizeof(uint32_t)));
    free(full.audio_words);
    free(half.audio_words);

    const int dimensions[][2] = {{1, 1}, {1, 5}, {5, 1}, {19, 17}};
    for (size_t index = 0; index < sizeof(dimensions) / sizeof(dimensions[0]);
         ++index) {
        char name[64];
        int count = snprintf(name, sizeof(name), "half-%dx%d.mp4", dimensions[index][0],
                             dimensions[index][1]);
        assert(count > 0 && (size_t)count < sizeof(name));
        char path[1024];
        make_path(path, directory, name);
        remove(path);
        CcPlatform platform = test_platform();
        platform.width = dimensions[index][0];
        platform.height = dimensions[index][1];
        platform.stride = (size_t)platform.width * 4;
        CcRecordingOptions options = test_options(&platform, NULL);
        options.half_size = true;
        CcRecording *recording = cc_recording_open_path(&options, path);
        assert(recording && cc_recording_frame(recording, 1000));
        assert(cc_recording_close(recording, 1000.03125));
        check_cleanup(&platform, NULL);
        TestMovie movie = read_movie(path);
        assert(movie.video_width == ((unsigned)platform.width + 1) / 2);
        assert(movie.video_height == ((unsigned)platform.height + 1) / 2);
        assert(movie.audio_frames == 1500);
        free(movie.audio_words);
    }
}

static void test_clock_and_source_failures(const char *directory) {
    char path[1024];
    CcPlatform platform = test_platform();
    CcRecordingOptions silent = test_options(&platform, NULL);
    for (unsigned mode = 0; mode < 3; ++mode) {
        const char *names[] = {"nan-close.mp4", "reversed-close.mp4",
                               "reversed-frame.mp4"};
        make_path(path, directory, names[mode]);
        remove(path);
        CcRecording *recording = cc_recording_open_path(&silent, path);
        assert(recording && cc_recording_frame(recording, 1000));
        assert(cc_recording_pump(recording, 1000.125));
        if (mode == 2) {
            assert(cc_recording_frame(recording, 1000.125));
            assert(!cc_recording_frame(recording, 1000.0625));
        }
        assert(!cc_recording_close(recording, mode ? 1000.0625 : NAN));
        check_cleanup(&platform, NULL);
        TestMovie movie = read_movie(path);
        assert(movie.video_ticks == 125000 && movie.video_frames == 1);
        assert(movie.audio_frames == 6000);
        free(movie.audio_words);
    }

    for (unsigned mode = 0; mode < 3; ++mode) {
        const char *names[] = {"invalid-pump.mp4", "invalid-stop.mp4",
                               "endless-stop.mp4"};
        make_path(path, directory, names[mode]);
        remove(path);
        TestAudio audio = {0};
        CcRecordingOptions options = test_options(&platform, &audio);
        options.sample_rate = 8000;
        CcRecording *recording = cc_recording_open_path(&options, path);
        assert(recording && cc_recording_frame(recording, 1000));
        assert(cc_recording_audio_start(recording, 1000));
        assert(cc_recording_frame(recording, 1000.125));
        audio.invalid_count = mode != 2;
        audio.endless = mode == 2;
        if (!mode)
            assert(!cc_recording_pump(recording, 1000.25));
        else
            assert(!cc_recording_audio_stop(recording, 1000.125));
        assert(!cc_recording_close(recording, 1000.25));
        check_cleanup(&platform, &audio);
        assert(audio.reads <= 17001);
    }
    make_path(path, directory, "source-overflow.mp4");
    remove(path);
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    CcRecording *recording = cc_recording_open_path(&options, path);
    assert(recording && cc_recording_frame(recording, 1000));
    assert(cc_recording_audio_start(recording, 1000));
    size_t overflow_frames = audio.capacity + 1;
    float *overflow = calloc(overflow_frames * 2, sizeof(float));
    assert(overflow);
    cc_audio_buffer_write(audio.buffer, overflow, overflow_frames);
    free(overflow);
    assert(!cc_recording_pump(recording, 1000.125));
    assert(strstr(cc_recording_error(recording), "overflowed"));
    assert(!cc_recording_close(recording, 1000.125));
    check_cleanup(&platform, &audio);

    make_path(path, directory, "unready-frame.mp4");
    remove(path);
    recording = cc_recording_open_path(&silent, path);
    assert(recording);
    platform.ready = false;
    assert(!cc_recording_frame(recording, 1000));
    assert(!cc_recording_close(recording, 1000));
    check_cleanup(&platform, NULL);
}

static void test_exclusive_and_movies(const char *directory) {
    char path[1024];
    make_path(path, directory, "exclusive.mp4");
    remove(path);
    const char marker[] = "preserve existing file";
    FILE *existing = fopen(path, "wb");
    assert(existing && fwrite(marker, 1, sizeof(marker), existing) == sizeof(marker));
    assert(fclose(existing) == 0);
    CcPlatform platform = test_platform();
    TestAudio audio = {0};
    CcRecordingOptions options = test_options(&platform, &audio);
    assert(!cc_recording_open_path(&options, path));
    check_cleanup(&platform, &audio);
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    assert(size == sizeof(marker) && !memcmp(bytes, marker, size));
    free(bytes);

    char home[1024];
    make_path(home, directory, "test-home");
    assert(mkdir(home, 0700) == 0 || errno == EEXIST);
    const char *previous = getenv("HOME");
    char *saved_home = previous ? strdup(previous) : NULL;
    assert(!previous || saved_home);
    assert(setenv("HOME", home, 1) == 0);
    CcRecording *recording = cc_recording_open(&options);
    assert(recording);
    int count = snprintf(path, sizeof(path), "%s", cc_recording_path(recording));
    assert(count > 0 && (size_t)count < sizeof(path));
    assert(!strncmp(path, home, strlen(home)) &&
           !strncmp(path + strlen(home), "/Movies/Capture_Test-", 21) &&
           !strcmp(path + strlen(path) - 4, ".mp4"));
    assert(cc_recording_frame(recording, 1000));
    assert(cc_recording_close(recording, 1000.125));
    check_cleanup(&platform, &audio);

    CcRecording *next = cc_recording_open(&options);
    assert(next && strcmp(cc_recording_path(next), path));
    char next_path[1024];
    count = snprintf(next_path, sizeof(next_path), "%s", cc_recording_path(next));
    assert(count > 0 && (size_t)count < sizeof(next_path));
    assert(cc_recording_frame(next, 1000));
    assert(cc_recording_close(next, 1000.125));
    check_cleanup(&platform, &audio);
    TestMovie movie = read_movie(path);
    assert(movie.audio_frames == 6000 && movie.video_ticks == 125000);
    free(movie.audio_words);
    assert(remove(path) == 0 && remove(next_path) == 0);
    if (saved_home)
        assert(setenv("HOME", saved_home, 1) == 0);
    else
        assert(unsetenv("HOME") == 0);
    free(saved_home);
}

int main(int argc, char **argv) {
    assert(argc == 2 && argv[1][0]);
    test_options_and_contract(argv[1]);
    test_exact_delay_prefill_and_stop(argv[1]);
    test_variable_rate_and_silent_pause(argv[1]);
    test_half_size_metadata_and_audio(argv[1]);
    test_clock_and_source_failures(argv[1]);
    test_exclusive_and_movies(argv[1]);
    puts("Shared recording coordinator tests passed.");
    return 0;
}
