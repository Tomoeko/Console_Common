#ifndef CONSOLE_COMMON_AUDIO_BUFFER_H
#define CONSOLE_COMMON_AUDIO_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcAudioBuffer CcAudioBuffer;

/* Create and destroy only while the producer is stopped. One producer writes
 * exact stereo float words, and one consumer reads them without locks or I/O.
 * Overflow preserves the queued prefix and permanently rejects later writes. */
CcAudioBuffer *cc_audio_buffer_create(size_t capacity_frames);
void cc_audio_buffer_write(CcAudioBuffer *buffer, const float *stereo, size_t frames);
size_t cc_audio_buffer_read(CcAudioBuffer *buffer, float *stereo,
                            size_t capacity_frames, uint64_t *first_sample_index);
bool cc_audio_buffer_failed(const CcAudioBuffer *buffer);
void cc_audio_buffer_destroy(CcAudioBuffer *buffer);

#endif
