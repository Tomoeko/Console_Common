#define _POSIX_C_SOURCE 200809L

#include "console_common/capture/capture_queue.h"
#include "console_common/capture/capture_writer.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    CAPTURE_QUEUE_VIDEO_SLOTS = 4,
    CAPTURE_QUEUE_JOBS = 64,
    CAPTURE_QUEUE_AUDIO_FRAMES = 4096
};

typedef struct {
    bool video;
    unsigned video_slot;
    uint32_t duration;
    size_t audio_frames;
} CaptureJob;

struct CcCaptureQueue {
    CcCaptureWriter *writer;
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    CaptureJob jobs[CAPTURE_QUEUE_JOBS];
    uint8_t *video_storage;
    float *audio_storage;
    size_t video_stride;
    size_t video_bytes;
    unsigned height;
    unsigned read;
    unsigned write;
    unsigned count;
    bool video_busy[CAPTURE_QUEUE_VIDEO_SLOTS];
    bool stopping;
    bool mutex_ready;
    bool condition_ready;
    bool worker_ready;
    atomic_bool failed;
    _Atomic(const char *) error;
};

static bool fail_queue(CcCaptureQueue *queue, const char *message) {
    const char *expected = NULL;
    atomic_compare_exchange_strong_explicit(&queue->error, &expected, message,
                                            memory_order_release, memory_order_relaxed);
    atomic_store_explicit(&queue->failed, true, memory_order_release);
    if (queue->condition_ready)
        pthread_cond_broadcast(&queue->condition);
    return false;
}

static float *audio_slot(CcCaptureQueue *queue, unsigned index) {
    return queue->audio_storage + (size_t)index * CAPTURE_QUEUE_AUDIO_FRAMES * 2;
}

static uint8_t *video_slot(CcCaptureQueue *queue, unsigned index) {
    return queue->video_storage + (size_t)index * queue->video_bytes;
}

static bool append_job(CcCaptureQueue *queue, const CaptureJob *job, unsigned index) {
    if (job->video)
        return cc_capture_writer_video(queue->writer,
                                       video_slot(queue, job->video_slot),
                                       queue->video_stride, job->duration);
    return cc_capture_writer_audio(queue->writer, audio_slot(queue, index),
                                   job->audio_frames);
}

static void *writer_worker(void *context) {
    CcCaptureQueue *queue = context;
    bool writing = true;
    if (pthread_mutex_lock(&queue->mutex) != 0) {
        fail_queue(queue, "Cannot lock the recording worker.");
        return NULL;
    }
    for (;;) {
        while (!queue->count && !queue->stopping) {
            if (pthread_cond_wait(&queue->condition, &queue->mutex) != 0) {
                fail_queue(queue, "Cannot wait for recording jobs.");
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            }
        }
        if (!queue->count)
            break;
        unsigned index = queue->read;
        CaptureJob job = queue->jobs[index];
        if (pthread_mutex_unlock(&queue->mutex) != 0) {
            fail_queue(queue, "Cannot unlock the recording worker.");
            return NULL;
        }
        if (writing && !append_job(queue, &job, index)) {
            writing = false;
            const char *reason = cc_capture_writer_error(queue->writer);
            fail_queue(queue, reason[0] ? reason : "Recording writer rejected a job.");
        }
        if (pthread_mutex_lock(&queue->mutex) != 0) {
            fail_queue(queue, "Cannot lock the recording worker.");
            return NULL;
        }
        if (job.video)
            queue->video_busy[job.video_slot] = false;
        queue->read = (queue->read + 1) % CAPTURE_QUEUE_JOBS;
        queue->count--;
        if (pthread_cond_broadcast(&queue->condition) != 0)
            fail_queue(queue, "Cannot notify the recording producer.");
    }
    if (pthread_mutex_unlock(&queue->mutex) != 0)
        fail_queue(queue, "Cannot unlock the recording worker.");
    return NULL;
}

static void release_queue(CcCaptureQueue *queue) {
    if (queue->condition_ready)
        pthread_cond_destroy(&queue->condition);
    if (queue->mutex_ready)
        pthread_mutex_destroy(&queue->mutex);
    free(queue->audio_storage);
    free(queue->video_storage);
    free(queue);
}

CcCaptureQueue *cc_capture_queue_open_with_audio(const char *path, unsigned width,
                                                 unsigned height,
                                                 uint32_t video_timescale,
                                                 uint32_t audio_rate,
                                                 CcCaptureAudioMode audio_mode) {
    if (!path || !path[0] || !video_timescale || !audio_rate || audio_rate > 65535 ||
        !width || !height || width > 4096 || height > 4096 ||
        (size_t)width > SIZE_MAX / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4) ||
        !cc_capture_audio_mode_supported(audio_mode))
        return NULL;
    size_t stride = (size_t)width * 4;
    size_t bytes = stride * height;
    if (bytes > SIZE_MAX / CAPTURE_QUEUE_VIDEO_SLOTS)
        return NULL;
    CcCaptureQueue *queue = calloc(1, sizeof(*queue));
    if (!queue)
        return NULL;
    atomic_init(&queue->failed, false);
    atomic_init(&queue->error, NULL);
    queue->video_stride = stride;
    queue->video_bytes = bytes;
    queue->height = height;
    queue->video_storage = malloc(bytes * CAPTURE_QUEUE_VIDEO_SLOTS);
    queue->audio_storage = malloc((size_t)CAPTURE_QUEUE_JOBS *
                                  CAPTURE_QUEUE_AUDIO_FRAMES * 2 * sizeof(float));
    if (!queue->video_storage || !queue->audio_storage)
        goto release_failed;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
        goto release_failed;
    queue->mutex_ready = true;
    if (pthread_cond_init(&queue->condition, NULL) != 0)
        goto release_failed;
    queue->condition_ready = true;
    queue->writer = cc_capture_writer_open_with_audio(
        path, width, height, video_timescale, audio_rate, audio_mode);
    if (!queue->writer)
        goto release_failed;
    if (pthread_create(&queue->worker, NULL, writer_worker, queue) != 0)
        goto release_failed;
    queue->worker_ready = true;
    return queue;

release_failed:
    if (queue->writer) {
        cc_capture_writer_close(queue->writer);
        remove(path); /* No recording owner was created for this exclusive file. */
    }
    release_queue(queue);
    return NULL;
}

CcCaptureQueue *cc_capture_queue_open(const char *path, unsigned width, unsigned height,
                                      uint32_t video_timescale, uint32_t audio_rate) {
    return cc_capture_queue_open_with_audio(path, width, height, video_timescale,
                                            audio_rate, CC_CAPTURE_AUDIO_NORMAL);
}

static unsigned available_video_slot(const CcCaptureQueue *queue) {
    unsigned slot = 0;
    while (slot < CAPTURE_QUEUE_VIDEO_SLOTS && queue->video_busy[slot])
        slot++;
    return slot;
}

static bool wait_deadline(struct timespec *deadline) {
    if (clock_gettime(CLOCK_REALTIME, deadline) != 0 || deadline->tv_sec < 0 ||
        (uintmax_t)deadline->tv_sec == UINTMAX_MAX)
        return false;
    time_t next_second = (time_t)((uintmax_t)deadline->tv_sec + 1);
    if (next_second <= deadline->tv_sec)
        return false;
    deadline->tv_sec = next_second;
    return true;
}

static bool reserve_job(CcCaptureQueue *queue, bool video, unsigned *job_index,
                        unsigned *frame_index) {
    if (pthread_mutex_lock(&queue->mutex) != 0)
        return fail_queue(queue, "Cannot lock the recording queue.");
    const char *reason = "Recording is closing or has failed.";
    bool waiting = false;
    bool okay = false;
    struct timespec deadline;
    while (!queue->stopping && !cc_capture_queue_failed(queue)) {
        unsigned slot = video ? available_video_slot(queue) : 0;
        if (queue->count < CAPTURE_QUEUE_JOBS &&
            (!video || slot < CAPTURE_QUEUE_VIDEO_SLOTS)) {
            if (video) {
                queue->video_busy[slot] = true;
                *frame_index = slot;
            }
            okay = true;
            break;
        }
        reason = queue->count == CAPTURE_QUEUE_JOBS
                     ? "Recording job queue remained full for one second."
                     : "Recording video queue remained full for one second.";
        if (!waiting && !wait_deadline(&deadline)) {
            reason = "Cannot read the recording wait clock.";
            break;
        }
        waiting = true;
        int status =
            pthread_cond_timedwait(&queue->condition, &queue->mutex, &deadline);
        if (status == ETIMEDOUT) {
            bool available =
                queue->count < CAPTURE_QUEUE_JOBS &&
                (!video || available_video_slot(queue) < CAPTURE_QUEUE_VIDEO_SLOTS);
            if (!available)
                break;
        } else if (status != 0) {
            reason = "Cannot wait for recording storage.";
            break;
        }
    }
    *job_index = queue->write;
    if (pthread_mutex_unlock(&queue->mutex) != 0) {
        okay = false;
        reason = "Cannot unlock the recording queue.";
    }
    if (!okay)
        return fail_queue(queue, reason);
    return true;
}

static bool publish_job(CcCaptureQueue *queue, unsigned index, CaptureJob job) {
    if (pthread_mutex_lock(&queue->mutex) != 0)
        return fail_queue(queue, "Cannot lock the recording queue.");
    bool okay = !queue->stopping && !cc_capture_queue_failed(queue);
    if (okay) {
        queue->jobs[index] = job;
        queue->write = (queue->write + 1) % CAPTURE_QUEUE_JOBS;
        queue->count++;
        if (pthread_cond_signal(&queue->condition) != 0)
            okay = false;
    } else if (job.video)
        queue->video_busy[job.video_slot] = false;
    if (pthread_mutex_unlock(&queue->mutex) != 0)
        okay = false;
    if (!okay)
        return fail_queue(queue, "Cannot publish a recording job.");
    return true;
}

bool cc_capture_queue_video(CcCaptureQueue *queue, const uint8_t *rgba,
                            size_t row_stride, uint32_t duration_ticks) {
    if (!queue)
        return false;
    if (!rgba || !duration_ticks || row_stride < queue->video_stride ||
        row_stride > SIZE_MAX / queue->height)
        return fail_queue(queue, "Invalid recording video input.");
    unsigned index = 0;
    unsigned slot = 0;
    if (!reserve_job(queue, true, &index, &slot))
        return false;
    uint8_t *frame = video_slot(queue, slot);
    /* The single owner reserves privately, so large copies do not hold the mutex. */
    for (unsigned row = 0; row < queue->height; ++row)
        memcpy(frame + (size_t)row * queue->video_stride,
               rgba + (size_t)row * row_stride, queue->video_stride);
    return publish_job(
        queue, index,
        (CaptureJob){.video = true, .video_slot = slot, .duration = duration_ticks});
}

bool cc_capture_queue_audio(CcCaptureQueue *queue, const float *stereo,
                            size_t frame_count) {
    if (!queue)
        return false;
    if (cc_capture_queue_failed(queue))
        return false;
    if (!frame_count)
        return true;
    if (!stereo || frame_count > CAPTURE_QUEUE_AUDIO_FRAMES)
        return fail_queue(queue, "Invalid recording audio input.");
    unsigned index = 0;
    unsigned unused_slot = 0;
    if (!reserve_job(queue, false, &index, &unused_slot))
        return false;
    memcpy(audio_slot(queue, index), stereo, frame_count * 2 * sizeof(float));
    return publish_job(queue, index, (CaptureJob){.audio_frames = frame_count});
}

bool cc_capture_queue_failed(const CcCaptureQueue *queue) {
    return queue && atomic_load_explicit(&queue->failed, memory_order_acquire);
}

const char *cc_capture_queue_error(const CcCaptureQueue *queue) {
    const char *error =
        queue ? atomic_load_explicit(&queue->error, memory_order_acquire) : NULL;
    return error ? error : "";
}

bool cc_capture_queue_close(CcCaptureQueue *queue) {
    if (!queue)
        return true;
    bool success = true;
    if (pthread_mutex_lock(&queue->mutex) != 0)
        success = fail_queue(queue, "Cannot lock the recording queue during close.");
    else {
        queue->stopping = true;
        if (pthread_cond_signal(&queue->condition) != 0)
            success =
                fail_queue(queue, "Cannot wake the recording worker during close.");
        if (pthread_mutex_unlock(&queue->mutex) != 0)
            success =
                fail_queue(queue, "Cannot unlock the recording queue during close.");
    }
    if (queue->worker_ready && pthread_join(queue->worker, NULL) != 0)
        return fail_queue(queue, "Cannot join the recording worker.");
    if (!cc_capture_writer_close(queue->writer) || cc_capture_queue_failed(queue))
        success = false;
    release_queue(queue);
    return success;
}
