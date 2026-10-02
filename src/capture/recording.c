#define _POSIX_C_SOURCE 200809L

#include "console_common/capture/recording.h"
#include "console_common/capture/capture_queue.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { RECORDING_TIMESCALE = 1000000, RECORDING_AUDIO_BLOCK = 4096 };

struct CcRecording {
    CcPlatform *platform;
    CcRecordingAudioSource audio;
    CcCaptureQueue *writer;
    uint8_t *frame;
    char *path;
    size_t stride;
    size_t frame_bytes;
    size_t source_stride;
    int width;
    int height;
    int source_width;
    int source_height;
    unsigned video_rate;
    unsigned audio_rate;
    uint64_t video_tick;
    uint64_t audio_frames;
    double origin;
    double last_time;
    bool audible;
    bool half_size;
    bool audio_running;
    bool started;
    bool failed;
    const char *error;
};

static bool fail_recording(CcRecording *recording, const char *error) {
    recording->failed = true;
    if (!recording->error)
        recording->error = error;
    return false;
}

static bool elapsed_count(const CcRecording *recording, double now, unsigned rate,
                          uint64_t *count) {
    if (!isfinite(now) || !recording->started || now < recording->last_time)
        return false;
    double value = floor((now - recording->origin) * rate);
    if (!isfinite(value) || value >= 0x1p64)
        return false;
    *count = (uint64_t)value;
    return true;
}

static bool silence_until(CcRecording *recording, uint64_t target, bool flush) {
    const float silence[RECORDING_AUDIO_BLOCK * 2] = {0};
    while (recording->audio_frames < target) {
        uint64_t remaining = target - recording->audio_frames;
        if (!flush && remaining < 512)
            break;
        size_t count = remaining < RECORDING_AUDIO_BLOCK ? (size_t)remaining
                                                         : RECORDING_AUDIO_BLOCK;
        if (!cc_capture_queue_audio(recording->writer, silence, count))
            return false;
        recording->audio_frames += count;
    }
    return true;
}

static bool audio_until(CcRecording *recording, uint64_t target, bool flush) {
    if (!recording->audible || !recording->audio_running)
        return silence_until(recording, target, flush);
    if (recording->audio.failed(recording->audio.context))
        return false;
    float stereo[RECORDING_AUDIO_BLOCK * 2];
    while (recording->audio_frames < target) {
        uint64_t remaining = target - recording->audio_frames;
        if (!flush && remaining < 512)
            break;
        size_t capacity = remaining < RECORDING_AUDIO_BLOCK ? (size_t)remaining
                                                            : RECORDING_AUDIO_BLOCK;
        size_t count =
            recording->audio.read(recording->audio.context, stereo, capacity);
        if (!count)
            break;
        if (count > capacity)
            return false;
        if (!cc_capture_queue_audio(recording->writer, stereo, count))
            return false;
        recording->audio_frames += count;
    }
    return true;
}

static bool discard_audio(CcRecording *recording) {
    if (!recording->audible)
        return true;
    float stereo[RECORDING_AUDIO_BLOCK * 2];
    size_t discarded = 0;
    size_t limit = (size_t)recording->audio_rate * 2;
    for (;;) {
        size_t count = recording->audio.read(recording->audio.context, stereo,
                                             RECORDING_AUDIO_BLOCK);
        if (!count)
            return true;
        if (count > RECORDING_AUDIO_BLOCK || count > limit - discarded)
            return false;
        discarded += count;
    }
}

static bool valid_prefix(const char *prefix) {
    if (!prefix)
        return true;
    size_t length = strlen(prefix);
    if (!length || length > 64)
        return false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)prefix[index];
        bool allowed = (value >= 'a' && value <= 'z') ||
                       (value >= 'A' && value <= 'Z') ||
                       (value >= '0' && value <= '9') || value == '-' || value == '_';
        if (!allowed)
            return false;
    }
    return true;
}

static bool valid_options(const CcRecordingOptions *options) {
    if (!options || !options->platform ||
        (options->video_rate != 50 && options->video_rate != 60) ||
        options->sample_rate < 8000 || options->sample_rate > 65535 ||
        !valid_prefix(options->filename_prefix))
        return false;
    bool audible = options->audio.begin != NULL;
    if (audible)
        return options->audio.read && options->audio.failed && options->audio.end;
    return !options->audio.read && !options->audio.failed && !options->audio.end;
}

CcRecording *cc_recording_open_path(const CcRecordingOptions *options,
                                    const char *path) {
    if (!valid_options(options) || !path || !*path)
        return NULL;
    CcFramebuffer framebuffer;
    CcPlatform *platform = options->platform;
    bool audible = options->audio.begin != NULL;
    unsigned audio_rate = options->sample_rate;
    if (!cc_platform_capture_begin(platform, &framebuffer))
        return NULL;
    CcRecording *recording = calloc(1, sizeof(*recording));
    if (!recording)
        goto release_capture;
    recording->platform = platform;
    recording->audio = options->audio;
    recording->audible = audible;
    recording->video_rate = options->video_rate;
    recording->audio_rate = audio_rate;
    if (framebuffer.width <= 0 || framebuffer.height <= 0 ||
        (size_t)framebuffer.width > SIZE_MAX / 4 ||
        framebuffer.stride < (size_t)framebuffer.width * 4 ||
        (size_t)framebuffer.height > SIZE_MAX / framebuffer.stride)
        goto release_recording;
    recording->stride = framebuffer.stride;
    recording->width = framebuffer.width;
    recording->height = framebuffer.height;
    recording->source_stride = framebuffer.stride;
    recording->source_width = framebuffer.width;
    recording->source_height = framebuffer.height;
    recording->half_size = options->half_size;
    if (recording->half_size) {
        recording->width = framebuffer.width / 2 + framebuffer.width % 2;
        recording->height = framebuffer.height / 2 + framebuffer.height % 2;
        recording->stride = (size_t)recording->width * 4;
    }
    recording->frame_bytes = recording->stride * (size_t)recording->height;
    recording->frame = malloc(recording->frame_bytes);
    recording->path = strdup(path);
    if (!recording->frame || !recording->path)
        goto release_recording;
    if (audible && !recording->audio.begin(recording->audio.context, audio_rate * 2))
        goto release_recording;
    recording->writer = cc_capture_queue_open(path, (unsigned)recording->width,
                                              (unsigned)recording->height,
                                              RECORDING_TIMESCALE, audio_rate);
    if (!recording->writer) {
        if (audible)
            recording->audio.end(recording->audio.context);
        goto release_recording;
    }
    return recording;

release_recording:
    free(recording->path);
    free(recording->frame);
    free(recording);
release_capture:
    cc_platform_capture_end(platform);
    return NULL;
}

static char *movies_path(const char *prefix) {
    const char *user_home = getenv("HOME");
    if (!user_home || !*user_home || strlen(user_home) > SIZE_MAX - strlen(prefix) - 80)
        return NULL;
    size_t capacity = strlen(user_home) + strlen(prefix) + 80;
    char *path = malloc(capacity);
    if (!path)
        return NULL;
    int count = snprintf(path, capacity, "%s/Movies", user_home);
    struct stat directory;
    if (count < 0 || (size_t)count >= capacity ||
        (mkdir(path, 0700) != 0 && errno != EEXIST) || stat(path, &directory) != 0 ||
        !S_ISDIR(directory.st_mode))
        goto release_path;
    time_t now = time(NULL);
    struct tm date;
    char stamp[24];
    if (!gmtime_r(&now, &date) ||
        !strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &date))
        goto release_path;
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
        count = snprintf(path, capacity, "%s/Movies/%s-%s-%04u.mp4", user_home, prefix,
                         stamp, suffix);
        if (count < 0 || (size_t)count >= capacity)
            break;
        if (lstat(path, &directory) != 0 && errno == ENOENT)
            return path;
    }
release_path:
    free(path);
    return NULL;
}

CcRecording *cc_recording_open(const CcRecordingOptions *options) {
    if (!valid_options(options))
        return NULL;
    const char *prefix =
        options->filename_prefix ? options->filename_prefix : "Recording";
    char *path = movies_path(prefix);
    if (!path)
        return NULL;
    CcRecording *recording = cc_recording_open_path(options, path);
    free(path);
    return recording;
}

const char *cc_recording_path(const CcRecording *recording) {
    return recording ? recording->path : NULL;
}

const char *cc_recording_error(const CcRecording *recording) {
    if (!recording)
        return "Recording is not active.";
    const char *queue_error = cc_capture_queue_error(recording->writer);
    if (*queue_error)
        return queue_error;
    if (recording->audible && recording->audio.failed(recording->audio.context))
        return "The recording audio buffer overflowed.";
    return recording->error ? recording->error : "Recording capture failed.";
}

static bool write_frame_until(CcRecording *recording, uint64_t target) {
    while (recording->video_tick < target) {
        uint64_t remaining = target - recording->video_tick;
        uint32_t duration = remaining < UINT32_MAX ? (uint32_t)remaining : UINT32_MAX;
        if (!cc_capture_queue_video(recording->writer, recording->frame,
                                    recording->stride, duration))
            return false;
        recording->video_tick += duration;
    }
    return true;
}

static void copy_half_frame(CcRecording *recording, const uint8_t *source) {
    size_t source_width = (size_t)recording->source_width;
    size_t source_height = (size_t)recording->source_height;
    for (size_t y = 0; y < (size_t)recording->height; ++y) {
        size_t top_y = y * 2;
        size_t bottom_y = top_y + 1 < source_height ? top_y + 1 : top_y;
        const uint8_t *top = source + top_y * recording->source_stride;
        const uint8_t *bottom = source + bottom_y * recording->source_stride;
        uint8_t *output = recording->frame + y * recording->stride;
        for (size_t x = 0; x < (size_t)recording->width; ++x) {
            size_t left = x * 2 * 4;
            size_t right = x * 2 + 1 < source_width ? left + 4 : left;
            for (size_t channel = 0; channel < 4; ++channel) {
                unsigned sum = (unsigned)top[left + channel] + top[right + channel] +
                               bottom[left + channel] + bottom[right + channel];
                output[x * 4 + channel] = (uint8_t)((sum + 2) / 4);
            }
        }
    }
}

bool cc_recording_frame(CcRecording *recording, double now) {
    if (!recording)
        return true;
    if (recording->failed || cc_capture_queue_failed(recording->writer) ||
        !isfinite(now))
        return fail_recording(recording, "The recording timestamp is invalid.");
    CcFramebuffer framebuffer;
    if (!cc_platform_capture_frame(recording->platform, &framebuffer) ||
        framebuffer.stride != recording->source_stride || !framebuffer.rgba ||
        framebuffer.width != recording->source_width ||
        framebuffer.height != recording->source_height)
        goto capture_failed;
    if (recording->started) {
        uint64_t target;
        if (!elapsed_count(recording, now, RECORDING_TIMESCALE, &target) ||
            target < recording->video_tick || !write_frame_until(recording, target))
            goto capture_failed;
    } else {
        recording->origin = now;
        recording->started = true;
    }
    recording->last_time = now;
    if (recording->half_size)
        copy_half_frame(recording, framebuffer.rgba);
    else
        memcpy(recording->frame, framebuffer.rgba, recording->frame_bytes);
    return true;

capture_failed:
    return fail_recording(recording,
                          "The recording framebuffer or timestamp is invalid.");
}

bool cc_recording_pump(CcRecording *recording, double now) {
    if (!recording || !recording->started)
        return true;
    uint64_t target;
    if (recording->failed || cc_capture_queue_failed(recording->writer) ||
        !elapsed_count(recording, now, recording->audio_rate, &target) ||
        !audio_until(recording, target, false)) {
        return fail_recording(recording, "Could not capture recording audio.");
    }
    recording->last_time = now;
    return true;
}

bool cc_recording_audio_start(CcRecording *recording, double now) {
    if (!recording || !recording->audible)
        return true;
    uint64_t target;
    if (!recording->started || recording->failed || recording->audio_running ||
        !elapsed_count(recording, now, recording->audio_rate, &target) ||
        !silence_until(recording, target, true))
        return false;
    recording->audio_running = true;
    recording->last_time = now;
    return true;
}

bool cc_recording_audio_stop(CcRecording *recording, double now) {
    if (!recording || !recording->audible)
        return true;
    uint64_t target;
    bool success = !recording->failed && recording->started &&
                   elapsed_count(recording, now, recording->audio_rate, &target) &&
                   audio_until(recording, target, true);
    recording->audio_running = false;
    if (!discard_audio(recording))
        success = false;
    if (!success)
        recording->failed = true;
    else
        recording->last_time = now;
    return success;
}

bool cc_recording_close(CcRecording *recording, double now) {
    if (!recording)
        return true;
    bool success = !recording->failed;
    if (recording->started && success) {
        bool clock_valid = isfinite(now) && now >= recording->last_time;
        if (!clock_valid)
            now = recording->last_time;
        uint64_t target = 0;
        success = elapsed_count(recording, now, RECORDING_TIMESCALE, &target);
        if (success && target <= recording->video_tick) {
            uint64_t duration = RECORDING_TIMESCALE / recording->video_rate;
            success = recording->video_tick <= UINT64_MAX - duration;
            if (success)
                target = recording->video_tick + duration;
        }
        if (success)
            success = write_frame_until(recording, target);
        uint64_t audio_target =
            target / RECORDING_TIMESCALE * recording->audio_rate +
            target % RECORDING_TIMESCALE * recording->audio_rate / RECORDING_TIMESCALE;
        if (success)
            success = audio_until(recording, audio_target, true) &&
                      silence_until(recording, audio_target, true);
        success = success && clock_valid;
    }
    if (recording->audible)
        recording->audio.end(recording->audio.context);
    cc_platform_capture_end(recording->platform);
    if (!cc_capture_queue_close(recording->writer))
        success = false;
    free(recording->frame);
    free(recording->path);
    free(recording);
    return success;
}
