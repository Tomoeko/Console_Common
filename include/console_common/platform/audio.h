#ifndef CONSOLE_COMMON_PLATFORM_AUDIO_H
#define CONSOLE_COMMON_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcAudio CcAudio;

typedef void (*CcAudioCompletion)(void *context, uint64_t completed_frames);

typedef struct CcAudioDescription {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frames_per_buffer;
    uint32_t buffer_count;
} CcAudioDescription;

typedef enum CcAudioWriteResult {
    CC_AUDIO_QUEUED,
    CC_AUDIO_FULL,
    CC_AUDIO_INVALID,
    CC_AUDIO_DEVICE_ERROR
} CcAudioWriteResult;

/* Interleaved binary32 PCM, in explicitly ordered device channels. Supported
   bounds are 1..32 channels, 1..8192 frames and 1..64 owned queue buffers.
   Validation preserves byte_count on failure; no allocations occur. */
bool cc_audio_description_validate(const CcAudioDescription *description,
                                   size_t *byte_count, char *error,
                                   size_t error_capacity);

/* Creation allocates the complete bounded queue; destroy synchronously ends
   callbacks and releases every buffer. All client calls are serialized by the
   caller. Native completion callbacks communicate through private atomics.
   The OS backend requires the actual device's requested channel count and
   sample rate, and maps channels in order. It does not invent a downmix,
   resampling rule, waveform, or missing-backend substitute. Native format
   change notifications invalidate further writes/start until recreated. */
CcAudio *cc_audio_create(const CcAudioDescription *description, char *error,
                         size_t error_capacity);
void cc_audio_destroy(CcAudio *audio);

/* Install one observer before the first buffer submission. The observer and
   its context remain borrowed until synchronous device destruction finishes.
   Calls run on the OS completion thread after the cumulative counter advances;
   they may signal a caller-owned wait primitive, but must not call audio client
   operations, allocate, or block on work performed by the audio client thread.
   Registration and client operations remain serialized. No observer changes
   are permitted after submission, including after an enqueue failure. */
bool cc_audio_set_completion(CcAudio *audio, CcAudioCompletion completion,
                             void *context, char *error, size_t error_capacity);

/* One exact buffer is copied from borrowed finite PCM. Full does not consume
   any samples. Write/start/stop allocate no application buffers. Priming and
   queue depth are caller choices; start requires queued data. Stop discards
   unplayed data synchronously. */
CcAudioWriteResult cc_audio_write(CcAudio *audio, const float *samples,
                                  size_t sample_count, char *error,
                                  size_t error_capacity);
bool cc_audio_start(CcAudio *audio, char *error, size_t error_capacity);
bool cc_audio_stop(CcAudio *audio, char *error, size_t error_capacity);
unsigned cc_audio_writable_buffers(const CcAudio *audio);

/* Completed means the OS returned a buffer for reuse; this is not a physical
   speaker timestamp. The cumulative counter includes buffer returns caused
   by a synchronous stop. Running is the OS's current queue state. */
uint64_t cc_audio_completed_frames(const CcAudio *audio);
bool cc_audio_running(CcAudio *audio, bool *running, char *error,
                      size_t error_capacity);

#endif
