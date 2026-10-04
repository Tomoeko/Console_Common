#include "console_common/platform/audio.h"

#include "console_common/support/error.h"

static bool unavailable(char *error, size_t capacity) {
    cc_error_set(error, capacity,
                 "Native audio backend is unavailable on this platform.");
    return false;
}

CcAudio *cc_audio_create(const CcAudioDescription *description, char *error,
                         size_t error_capacity) {
    size_t byte_count;
    if (!cc_audio_description_validate(description, &byte_count, error, error_capacity))
        return NULL;
    unavailable(error, error_capacity);
    return NULL;
}

void cc_audio_destroy(CcAudio *audio) {
    (void)audio;
}

bool cc_audio_set_completion(CcAudio *audio, CcAudioCompletion completion,
                             void *context, char *error, size_t error_capacity) {
    (void)audio;
    (void)completion;
    (void)context;
    return unavailable(error, error_capacity);
}

CcAudioWriteResult cc_audio_write(CcAudio *audio, const float *samples,
                                  size_t sample_count, char *error,
                                  size_t error_capacity) {
    (void)audio;
    (void)samples;
    (void)sample_count;
    unavailable(error, error_capacity);
    return CC_AUDIO_DEVICE_ERROR;
}

bool cc_audio_start(CcAudio *audio, char *error, size_t error_capacity) {
    (void)audio;
    return unavailable(error, error_capacity);
}

bool cc_audio_stop(CcAudio *audio, char *error, size_t error_capacity) {
    (void)audio;
    return unavailable(error, error_capacity);
}

unsigned cc_audio_writable_buffers(const CcAudio *audio) {
    (void)audio;
    return 0;
}

uint64_t cc_audio_completed_frames(const CcAudio *audio) {
    (void)audio;
    return 0;
}

bool cc_audio_running(CcAudio *audio, bool *running, char *error,
                      size_t error_capacity) {
    (void)audio;
    (void)running;
    return unavailable(error, error_capacity);
}
