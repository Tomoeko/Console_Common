#include "console_common/capture/audio_buffer.h"

#include <stdatomic.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct CcAudioBuffer {
    float *samples;
    unsigned slots;
    atomic_uint read;
    atomic_uint write;
    atomic_bool failed;
    uint64_t first_sample;
};

CcAudioBuffer *cc_audio_buffer_create(size_t capacity_frames) {
    if (!capacity_frames || capacity_frames >= UINT_MAX ||
        capacity_frames >= SIZE_MAX / (2 * sizeof(float)))
        return NULL;
    CcAudioBuffer *capture = calloc(1, sizeof(*capture));
    if (!capture)
        return NULL;
    capture->slots = (unsigned)capacity_frames + 1;
    atomic_init(&capture->read, 0);
    atomic_init(&capture->write, 0);
    atomic_init(&capture->failed, false);
    /* A platform with locking atomic emulation cannot use this real-time tee. */
    if (!atomic_is_lock_free(&capture->read) || !atomic_is_lock_free(&capture->write) ||
        !atomic_is_lock_free(&capture->failed)) {
        free(capture);
        return NULL;
    }
    capture->samples = malloc((size_t)capture->slots * 2 * sizeof(float));
    if (!capture->samples) {
        free(capture);
        return NULL;
    }
    return capture;
}

static size_t available_frames(const CcAudioBuffer *capture, unsigned read,
                               unsigned write) {
    return write >= read ? write - read : capture->slots - read + write;
}

void cc_audio_buffer_write(CcAudioBuffer *capture, const float *stereo, size_t frames) {
    if (!capture || !frames ||
        atomic_load_explicit(&capture->failed, memory_order_relaxed))
        return;
    unsigned write = atomic_load_explicit(&capture->write, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&capture->read, memory_order_acquire);
    size_t available = available_frames(capture, read, write);
    if (!stereo || frames > capture->slots - 1 - available) {
        atomic_store_explicit(&capture->failed, true, memory_order_release);
        return;
    }
    size_t first = capture->slots - write;
    if (first > frames)
        first = frames;
    memcpy(capture->samples + (size_t)write * 2, stereo, first * 2 * sizeof(float));
    size_t remaining = frames - first;
    if (remaining)
        memcpy(capture->samples, stereo + first * 2, remaining * 2 * sizeof(float));
    unsigned next = remaining ? (unsigned)remaining : write + (unsigned)first;
    if (next == capture->slots)
        next = 0;
    /* Publish only after both sides of a wrapped block are complete. */
    atomic_store_explicit(&capture->write, next, memory_order_release);
}

size_t cc_audio_buffer_read(CcAudioBuffer *capture, float *stereo,
                            size_t capacity_frames, uint64_t *first_sample_index) {
    if (!capture || !stereo || !capacity_frames ||
        capacity_frames > SIZE_MAX / (2 * sizeof(float)))
        return 0;
    unsigned read = atomic_load_explicit(&capture->read, memory_order_relaxed);
    unsigned write = atomic_load_explicit(&capture->write, memory_order_acquire);
    size_t count = available_frames(capture, read, write);
    if (count > capacity_frames)
        count = capacity_frames;
    if (count > UINT64_MAX - capture->first_sample) {
        atomic_store_explicit(&capture->failed, true, memory_order_release);
        return 0;
    }
    if (first_sample_index)
        *first_sample_index = capture->first_sample;
    size_t first = capture->slots - read;
    if (first > count)
        first = count;
    memcpy(stereo, capture->samples + (size_t)read * 2, first * 2 * sizeof(float));
    size_t remaining = count - first;
    if (remaining)
        memcpy(stereo + first * 2, capture->samples, remaining * 2 * sizeof(float));
    unsigned next = remaining ? (unsigned)remaining : read + (unsigned)first;
    if (next == capture->slots)
        next = 0;
    capture->first_sample += count;
    /* The producer may reuse these slots only after the copy has finished. */
    atomic_store_explicit(&capture->read, next, memory_order_release);
    return count;
}

bool cc_audio_buffer_failed(const CcAudioBuffer *capture) {
    return capture && atomic_load_explicit(&capture->failed, memory_order_acquire);
}

void cc_audio_buffer_destroy(CcAudioBuffer *capture) {
    if (!capture)
        return;
    free(capture->samples);
    free(capture);
}
