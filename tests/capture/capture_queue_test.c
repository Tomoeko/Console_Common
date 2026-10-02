#define _POSIX_C_SOURCE 200809L

#include "console_common/capture/capture_writer.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static bool gate_closed;
static bool fail_thread;
static bool fail_open;
static int allocation_failure = -1;
static int append_failure = -1;
static bool verify_copied_media;
static unsigned copied_video_frames;

static void *test_allocate(size_t size) {
    if (allocation_failure >= 0 && allocation_failure-- == 0)
        return NULL;
    return malloc(size);
}

static void *test_zero_allocate(size_t count, size_t size) {
    if (allocation_failure >= 0 && allocation_failure-- == 0)
        return NULL;
    return calloc(count, size);
}

static int test_thread_create(pthread_t *thread, const pthread_attr_t *attributes,
                              void *(*entry)(void *), void *context) {
    return fail_thread ? EAGAIN : pthread_create(thread, attributes, entry, context);
}

static CcCaptureWriter *test_writer_open(const char *path, unsigned width,
                                         unsigned height, uint32_t video_timescale,
                                         uint32_t audio_rate) {
    return fail_open ? NULL
                     : cc_capture_writer_open(path, width, height, video_timescale,
                                              audio_rate);
}

static bool wait_for_writer(void) {
    assert(pthread_mutex_lock(&gate_mutex) == 0);
    while (gate_closed)
        assert(pthread_cond_wait(&gate_condition, &gate_mutex) == 0);
    bool okay = append_failure < 0 || append_failure-- != 0;
    assert(pthread_mutex_unlock(&gate_mutex) == 0);
    return okay;
}

static bool test_writer_video(CcCaptureWriter *writer, const uint8_t *rgba,
                              size_t row_stride, uint32_t duration) {
    if (!wait_for_writer())
        return false;
    if (verify_copied_media) {
        for (unsigned y = 0; y < 17; ++y) {
            for (unsigned x = 0; x < 19 * 4; ++x) {
                uint8_t expected =
                    (uint8_t)((y * 96 + x) * 43 + copied_video_frames * 61);
                assert(rgba[(size_t)y * row_stride + x] == expected);
            }
        }
        copied_video_frames++;
    }
    return cc_capture_writer_video(writer, rgba, row_stride, duration);
}

static bool test_writer_audio(CcCaptureWriter *writer, const float *stereo,
                              size_t frames) {
    if (!wait_for_writer())
        return false;
    if (verify_copied_media) {
        assert(frames == 16 + (copied_video_frames - 1) % 4);
        for (size_t i = 0; i < frames * 2; ++i) {
            float expected = (float)((int)i - 127) / 128;
            assert(!memcmp(stereo + i, &expected, sizeof(expected)));
        }
    }
    return cc_capture_writer_audio(writer, stereo, frames);
}

/* Gate and fault-inject the real worker while retaining the real MP4 writer. */
#define malloc test_allocate
#define calloc test_zero_allocate
#define pthread_create test_thread_create
#define cc_capture_writer_open test_writer_open
#define cc_capture_writer_video test_writer_video
#define cc_capture_writer_audio test_writer_audio
#include "../../src/capture/capture_queue.c"
#undef cc_capture_writer_audio
#undef cc_capture_writer_video
#undef cc_capture_writer_open
#undef pthread_create
#undef calloc
#undef malloc

static void set_gate(bool closed) {
    assert(pthread_mutex_lock(&gate_mutex) == 0);
    gate_closed = closed;
    assert(pthread_cond_broadcast(&gate_condition) == 0);
    assert(pthread_mutex_unlock(&gate_mutex) == 0);
}

static void make_path(char output[1024], const char *directory, const char *name) {
    int count = snprintf(output, 1024, "%s/%s", directory, name);
    assert(count > 0 && count < 1024);
    remove(output);
}

static void compare_files(const char *expected_path, const char *actual_path) {
    FILE *expected = fopen(expected_path, "rb");
    FILE *actual = fopen(actual_path, "rb");
    assert(expected && actual);
    uint8_t expected_bytes[4096];
    uint8_t actual_bytes[4096];
    size_t count;
    do {
        count = fread(expected_bytes, 1, sizeof(expected_bytes), expected);
        size_t actual_count = fread(actual_bytes, 1, sizeof(actual_bytes), actual);
        assert(count == actual_count && !memcmp(expected_bytes, actual_bytes, count));
    } while (count);
    assert(!ferror(expected) && !ferror(actual));
    assert(fclose(expected) == 0 && fclose(actual) == 0);
}

static void wait_for_empty(CcCaptureQueue *queue) {
    for (unsigned attempt = 0; attempt < 2000; ++attempt) {
        assert(pthread_mutex_lock(&queue->mutex) == 0);
        bool empty = queue->count == 0;
        assert(pthread_mutex_unlock(&queue->mutex) == 0);
        if (empty)
            return;
        struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }
    assert(false);
}

static void test_copied_media(const char *directory) {
    char actual_path[1024];
    char expected_path[1024];
    make_path(actual_path, directory, "capture-queued.mp4");
    make_path(expected_path, directory, "capture-direct.mp4");
    verify_copied_media = true;
    copied_video_frames = 0;
    CcCaptureQueue *queue = cc_capture_queue_open(actual_path, 19, 17, 60000, 48000);
    CcCaptureWriter *direct =
        cc_capture_writer_open(expected_path, 19, 17, 60000, 48000);
    assert(queue && direct);
    allocation_failure = 0;
    uint8_t rgba[17 * 96];
    float stereo[256];
    for (unsigned frame = 0; frame < 80; ++frame) {
        for (size_t i = 0; i < sizeof(rgba); ++i)
            rgba[i] = (uint8_t)(i * 43 + frame * 61);
        for (size_t i = 0; i < sizeof(stereo) / sizeof(stereo[0]); ++i)
            stereo[i] = (float)((int)i - 127) / 128;
        size_t audio_frames = 16 + frame % 4;
        assert(cc_capture_writer_video(direct, rgba, 96, 1001 + frame));
        assert(cc_capture_writer_audio(direct, stereo, audio_frames));
        assert(cc_capture_queue_video(queue, rgba, 96, 1001 + frame));
        assert(cc_capture_queue_audio(queue, stereo, audio_frames));
        memset(rgba, 0, sizeof(rgba));
        memset(stereo, 0, sizeof(stereo));
        if (frame % 4 == 3)
            wait_for_empty(queue);
    }
    assert(cc_capture_queue_audio(queue, NULL, 0));
    assert(!cc_capture_queue_failed(queue));
    assert(!cc_capture_queue_error(queue)[0]);
    assert(cc_capture_queue_close(queue));
    assert(copied_video_frames == 80);
    verify_copied_media = false;
    assert(cc_capture_writer_close(direct));
    assert(allocation_failure == 0); /* Enqueue and drain use only initial storage. */
    allocation_failure = -1;
    compare_files(expected_path, actual_path);
    remove(actual_path);
    remove(expected_path);
}

static void test_overflow_prefix(const char *directory, bool video) {
    char actual_path[1024];
    char expected_path[1024];
    make_path(actual_path, directory, "capture-queue-overflow.mp4");
    make_path(expected_path, directory, "capture-overflow-direct.mp4");
    set_gate(true);
    CcCaptureQueue *queue = cc_capture_queue_open(actual_path, 1, 1, 60, 48000);
    CcCaptureWriter *direct = cc_capture_writer_open(expected_path, 1, 1, 60, 48000);
    assert(queue && direct);
    uint8_t rgba[4] = {7, 11, 17, 255};
    float stereo[2] = {0.5f, -0.5f};
    unsigned jobs = video ? CAPTURE_QUEUE_VIDEO_SLOTS : CAPTURE_QUEUE_JOBS;
    for (unsigned i = 0; i < jobs; ++i) {
        if (video) {
            assert(cc_capture_queue_video(queue, rgba, 4, i + 1));
            assert(cc_capture_writer_video(direct, rgba, 4, i + 1));
        } else {
            assert(cc_capture_queue_audio(queue, stereo, 1));
            assert(cc_capture_writer_audio(direct, stereo, 1));
        }
    }
    assert(!(video ? cc_capture_queue_video(queue, rgba, 4, 1)
                   : cc_capture_queue_audio(queue, stereo, 1)));
    assert(cc_capture_queue_failed(queue));
    const char *reason = video ? "Recording video queue remained full for one second."
                               : "Recording job queue remained full for one second.";
    assert(!strcmp(cc_capture_queue_error(queue), reason));
    assert(!cc_capture_queue_audio(queue, stereo, 1));
    assert(!cc_capture_queue_video(queue, rgba, 4, 1));
    assert(!strcmp(cc_capture_queue_error(queue), reason));
    set_gate(false);
    assert(!cc_capture_queue_close(queue));
    assert(cc_capture_writer_close(direct));
    compare_files(expected_path, actual_path);
    remove(actual_path);
    remove(expected_path);
}

static void *release_gate_later(void *context) {
    (void)context;
    struct timespec delay = {.tv_nsec = 50000000};
    assert(nanosleep(&delay, NULL) == 0);
    set_gate(false);
    return NULL;
}

static void test_backpressure(const char *directory, bool video) {
    char actual_path[1024];
    char expected_path[1024];
    make_path(actual_path, directory, "capture-queue-backpressure.mp4");
    make_path(expected_path, directory, "capture-backpressure-direct.mp4");
    set_gate(true);
    CcCaptureQueue *queue = cc_capture_queue_open(actual_path, 1, 1, 60, 48000);
    CcCaptureWriter *direct = cc_capture_writer_open(expected_path, 1, 1, 60, 48000);
    assert(queue && direct);
    uint8_t rgba[4] = {7, 11, 17, 255};
    float stereo[2] = {0.5f, -0.5f};
    unsigned jobs = video ? CAPTURE_QUEUE_VIDEO_SLOTS : CAPTURE_QUEUE_JOBS;
    for (unsigned i = 0; i < jobs; ++i) {
        assert(video ? cc_capture_queue_video(queue, rgba, 4, 1)
                     : cc_capture_queue_audio(queue, stereo, 1));
        assert(video ? cc_capture_writer_video(direct, rgba, 4, 1)
                     : cc_capture_writer_audio(direct, stereo, 1));
    }
    pthread_t release_thread;
    struct timespec started;
    struct timespec finished;
    assert(clock_gettime(CLOCK_MONOTONIC, &started) == 0);
    assert(pthread_create(&release_thread, NULL, release_gate_later, NULL) == 0);
    assert(video ? cc_capture_queue_video(queue, rgba, 4, 1)
                 : cc_capture_queue_audio(queue, stereo, 1));
    assert(clock_gettime(CLOCK_MONOTONIC, &finished) == 0);
    double elapsed = (double)(finished.tv_sec - started.tv_sec) +
                     (double)(finished.tv_nsec - started.tv_nsec) * 1e-9;
    assert(elapsed >= 0.04 && elapsed < 1.0);
    assert(pthread_join(release_thread, NULL) == 0);
    assert(!cc_capture_queue_error(queue)[0] && !cc_capture_queue_failed(queue));
    assert(video ? cc_capture_writer_video(direct, rgba, 4, 1)
                 : cc_capture_writer_audio(direct, stereo, 1));
    assert(cc_capture_queue_close(queue) && cc_capture_writer_close(direct));
    compare_files(expected_path, actual_path);
    remove(actual_path);
    remove(expected_path);
}

static void test_open_failures(const char *directory) {
    char path[1024];
    make_path(path, directory, "capture-queue-failure.mp4");
    assert(!cc_capture_queue_open(NULL, 1, 1, 60, 48000));
    assert(!cc_capture_queue_open(path, 0, 1, 60, 48000));
    assert(!cc_capture_queue_open(path, 4097, 1, 60, 48000));
    assert(!cc_capture_queue_open(path, 1, 1, 0, 48000));
    for (int allocation = 0; allocation < 3; ++allocation) {
        allocation_failure = allocation;
        assert(!cc_capture_queue_open(path, 1, 1, 60, 48000));
    }
    allocation_failure = -1;
    fail_open = true;
    assert(!cc_capture_queue_open(path, 1, 1, 60, 48000));
    fail_open = false;
    fail_thread = true;
    assert(!cc_capture_queue_open(path, 1, 1, 60, 48000));
    fail_thread = false;
    remove(path);
}

static void test_rejected_media(const char *directory, unsigned mode) {
    char path[1024];
    make_path(path, directory, "capture-queue-rejected.mp4");
    CcCaptureQueue *queue = cc_capture_queue_open(path, 1, 1, 60, 48000);
    assert(queue);
    uint8_t rgba[4] = {0};
    float stereo[2] = {0};
    if (mode == 0)
        assert(!cc_capture_queue_video(queue, NULL, 4, 1));
    else if (mode == 1)
        assert(!cc_capture_queue_video(queue, rgba, 3, 1));
    else if (mode == 2)
        assert(!cc_capture_queue_video(queue, rgba, 4, 0));
    else if (mode == 3)
        assert(!cc_capture_queue_audio(queue, NULL, 1));
    else
        assert(!cc_capture_queue_audio(queue, stereo, 4097));
    assert(cc_capture_queue_failed(queue));
    const char *reason =
        mode < 3 ? "Invalid recording video input." : "Invalid recording audio input.";
    assert(!strcmp(cc_capture_queue_error(queue), reason));
    assert(!cc_capture_queue_close(queue));
    remove(path);
}

static void test_worker_failure(const char *directory) {
    char path[1024];
    char expected_path[1024];
    make_path(path, directory, "capture-worker-failure.mp4");
    make_path(expected_path, directory, "capture-worker-prefix.mp4");
    set_gate(true);
    assert(pthread_mutex_lock(&gate_mutex) == 0);
    append_failure = 0;
    assert(pthread_mutex_unlock(&gate_mutex) == 0);
    CcCaptureQueue *queue = cc_capture_queue_open(path, 1, 1, 60, 48000);
    assert(queue);
    float stereo[2] = {0};
    assert(cc_capture_queue_audio(queue, stereo, 1));
    assert(cc_capture_queue_audio(queue, stereo, 1));
    set_gate(false);
    assert(!cc_capture_queue_close(queue));
    CcCaptureWriter *empty = cc_capture_writer_open(expected_path, 1, 1, 60, 48000);
    assert(empty && cc_capture_writer_close(empty));
    compare_files(expected_path, path);
    assert(pthread_mutex_lock(&gate_mutex) == 0);
    append_failure = -1;
    assert(pthread_mutex_unlock(&gate_mutex) == 0);
    remove(path);
    remove(expected_path);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(!cc_capture_queue_failed(NULL));
    assert(!cc_capture_queue_error(NULL)[0]);
    assert(!cc_capture_queue_video(NULL, NULL, 0, 0));
    assert(!cc_capture_queue_audio(NULL, NULL, 0));
    assert(cc_capture_queue_close(NULL));
    test_copied_media(argv[1]);
    test_overflow_prefix(argv[1], true);
    test_overflow_prefix(argv[1], false);
    test_backpressure(argv[1], true);
    test_backpressure(argv[1], false);
    test_open_failures(argv[1]);
    for (unsigned mode = 0; mode < 5; ++mode)
        test_rejected_media(argv[1], mode);
    test_worker_failure(argv[1]);
    assert(pthread_cond_destroy(&gate_condition) == 0);
    assert(pthread_mutex_destroy(&gate_mutex) == 0);
    return 0;
}
