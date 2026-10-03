#ifndef CC_CAPTURE_QUEUE_H
#define CC_CAPTURE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "console_common/capture/capture_writer.h"

typedef struct CcCaptureQueue CcCaptureQueue;

/* One owner serializes these calls. A worker exclusively appends to the file.
 * Four reusable video slots and 64 jobs bound memory. Full storage waits up to
 * one second for the worker; timeout fails explicitly instead of dropping media.
 * These calls must never run from the audio callback. */
CcCaptureQueue *cc_capture_queue_open(const char *path, unsigned width, unsigned height,
                                      uint32_t video_timescale, uint32_t audio_rate);
CcCaptureQueue *cc_capture_queue_open_with_audio(const char *path, unsigned width,
                                                 unsigned height,
                                                 uint32_t video_timescale,
                                                 uint32_t audio_rate,
                                                 CcCaptureAudioMode audio_mode);
bool cc_capture_queue_video(CcCaptureQueue *queue, const uint8_t *rgba,
                            size_t row_stride, uint32_t duration_ticks);
bool cc_capture_queue_audio(CcCaptureQueue *queue, const float *stereo,
                            size_t frame_count);
bool cc_capture_queue_failed(const CcCaptureQueue *queue);
/* Borrowed diagnostic remains valid until close; reading never touches worker state. */
const char *cc_capture_queue_error(const CcCaptureQueue *queue);

/* Drains accepted jobs, joins the worker, finalizes the file, and frees storage.
 * This returns false after any enqueue/worker failure, even if its prefix is valid. */
bool cc_capture_queue_close(CcCaptureQueue *queue);

#endif
