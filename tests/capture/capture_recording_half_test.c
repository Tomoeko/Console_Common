#include "console_common/capture/capture_queue.h"
#include "console_common/capture/recording.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_MAX_PIXELS = 32 * 32 };

struct CcPlatform {
    uint8_t *pixels;
    int width;
    int height;
    size_t stride;
    bool active;
};

struct CcCaptureQueue {
    uint8_t expected[2][TEST_MAX_PIXELS * 4];
    unsigned width;
    unsigned height;
    size_t stride;
    size_t audio_frames;
    unsigned video_frames;
    bool active;
};

static CcCaptureQueue test_queue;

bool cc_platform_capture_begin(CcPlatform *platform, CcFramebuffer *frame) {
    assert(platform && frame && !platform->active);
    platform->active = true;
    *frame = (CcFramebuffer){.width = platform->width,
                             .height = platform->height,
                             .stride = platform->stride};
    return true;
}

bool cc_platform_capture_frame(CcPlatform *platform, CcFramebuffer *frame) {
    assert(platform && frame && platform->active);
    *frame = (CcFramebuffer){.rgba = platform->pixels,
                             .width = platform->width,
                             .height = platform->height,
                             .stride = platform->stride};
    return true;
}

void cc_platform_capture_end(CcPlatform *platform) {
    assert(platform && platform->active);
    platform->active = false;
}

CcCaptureQueue *cc_capture_queue_open(const char *path, unsigned width, unsigned height,
                                      uint32_t timescale, uint32_t audio_rate) {
    assert(path && !test_queue.active);
    assert(width == test_queue.width && height == test_queue.height);
    assert(timescale == 1000000 && audio_rate == 48000);
    test_queue.active = true;
    return &test_queue;
}

bool cc_capture_queue_video(CcCaptureQueue *queue, const uint8_t *rgba,
                            size_t row_stride, uint32_t duration) {
    assert(queue == &test_queue && queue->active && rgba);
    assert(row_stride == queue->stride && duration == 31250);
    assert(queue->video_frames < 2);
    for (unsigned y = 0; y < queue->height; ++y) {
        const uint8_t *expected =
            queue->expected[queue->video_frames] + (size_t)y * queue->width * 4;
        assert(
            !memcmp(rgba + (size_t)y * row_stride, expected, (size_t)queue->width * 4));
    }
    ++queue->video_frames;
    return true;
}

bool cc_capture_queue_audio(CcCaptureQueue *queue, const float *stereo, size_t frames) {
    assert(queue == &test_queue && queue->active && stereo && frames);
    for (size_t sample = 0; sample < frames * 2; ++sample) {
        uint32_t word;
        memcpy(&word, stereo + sample, sizeof(word));
        assert(word == 0);
    }
    queue->audio_frames += frames;
    return true;
}

bool cc_capture_queue_failed(const CcCaptureQueue *queue) {
    assert(queue == &test_queue && queue->active);
    return false;
}

const char *cc_capture_queue_error(const CcCaptureQueue *queue) {
    assert(queue == &test_queue && queue->active);
    return "";
}

bool cc_capture_queue_close(CcCaptureQueue *queue) {
    assert(queue == &test_queue && queue->active);
    queue->active = false;
    return true;
}

static void fill_source(CcPlatform *platform, unsigned phase) {
    memset(platform->pixels, 0xcd, platform->stride * (size_t)platform->height);
    for (int y = 0; y < platform->height; ++y) {
        for (int x = 0; x < platform->width; ++x) {
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned value =
                    (unsigned)x * 31 + (unsigned)y * 43 + channel * 57 + phase * 79;
                platform
                    ->pixels[(size_t)y * platform->stride + (size_t)x * 4 + channel] =
                    (uint8_t)value;
            }
        }
    }
}

static void expected_frame(const CcPlatform *platform, uint8_t *expected,
                           bool half_size) {
    unsigned step = half_size ? 2 : 1;
    unsigned width = (unsigned)platform->width;
    unsigned height = (unsigned)platform->height;
    size_t output = 0;
    for (unsigned y = 0; y < height; y += step) {
        for (unsigned x = 0; x < width; x += step) {
            unsigned columns = width - x < step ? width - x : step;
            unsigned rows = height - y < step ? height - y : step;
            unsigned count = columns * rows;
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned sum = 0;
                for (unsigned dy = 0; dy < rows; ++dy) {
                    for (unsigned dx = 0; dx < columns; ++dx) {
                        size_t index = (size_t)(y + dy) * platform->stride +
                                       (size_t)(x + dx) * 4 + channel;
                        sum += platform->pixels[index];
                    }
                }
                expected[output++] = (uint8_t)((sum + count / 2) / count);
            }
        }
    }
}

static void test_filtered_pixels(unsigned width, unsigned height, bool half_size) {
    CcPlatform platform = {
        .width = (int)width, .height = (int)height, .stride = (size_t)width * 4 + 5};
    platform.pixels = malloc(platform.stride * height);
    assert(platform.pixels);
    test_queue = (CcCaptureQueue){.width = half_size ? (width + 1) / 2 : width,
                                  .height = half_size ? (height + 1) / 2 : height};
    test_queue.stride = half_size ? (size_t)test_queue.width * 4 : platform.stride;
    fill_source(&platform, 0);
    expected_frame(&platform, test_queue.expected[0], half_size);
    CcRecordingOptions options = {.platform = &platform,
                                  .sample_rate = 48000,
                                  .video_rate = 60,
                                  .half_size = half_size};
    CcRecording *recording = cc_recording_open_path(&options, "unused.mp4");
    assert(recording && cc_recording_frame(recording, 1000));
    fill_source(&platform, 1);
    expected_frame(&platform, test_queue.expected[1], half_size);
    assert(cc_recording_frame(recording, 1000.03125));
    assert(cc_recording_close(recording, 1000.0625));
    assert(!platform.active && !test_queue.active && test_queue.video_frames == 2);
    assert(test_queue.audio_frames == 3000);
    free(platform.pixels);
}

static void test_full_source_validation(void) {
    uint8_t pixels[4 * 4 * 4] = {0};
    CcPlatform platform = {.pixels = pixels, .width = 4, .height = 4, .stride = 16};
    test_queue = (CcCaptureQueue){.width = 2, .height = 2, .stride = 8};
    CcRecordingOptions options = {.platform = &platform,
                                  .sample_rate = 48000,
                                  .video_rate = 60,
                                  .half_size = true};
    CcRecording *recording = cc_recording_open_path(&options, "unused.mp4");
    assert(recording);
    platform.width = 2;
    platform.height = 2;
    platform.stride = 8;
    assert(!cc_recording_frame(recording, 1000));
    assert(!cc_recording_close(recording, 1000));
    assert(!platform.active && !test_queue.active && !test_queue.video_frames);
}

int main(void) {
    const unsigned dimensions[][2] = {{1, 1}, {1, 5}, {5, 1},  {2, 2},
                                      {3, 3}, {4, 6}, {19, 17}};
    for (size_t index = 0; index < sizeof(dimensions) / sizeof(dimensions[0]);
         ++index) {
        test_filtered_pixels(dimensions[index][0], dimensions[index][1], true);
        test_filtered_pixels(dimensions[index][0], dimensions[index][1], false);
    }
    test_full_source_validation();
    puts("Half-size recording pixel tests passed.");
    return 0;
}
