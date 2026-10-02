#ifndef CC_CAPTURE_WRITER_H
#define CC_CAPTURE_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcCaptureWriter CcCaptureWriter;

/* Calls are serialized by the owner. Opening never overwrites an existing file. */
CcCaptureWriter *cc_capture_writer_open(const char *path, unsigned width,
                                        unsigned height, uint32_t video_timescale,
                                        uint32_t audio_rate);

/* Video uses the platform's high-quality codec; the opaque track omits alpha.
 * Audio preserves every interleaved IEEE-754 binary32 sample word exactly. */
bool cc_capture_writer_video(CcCaptureWriter *writer, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks);
bool cc_capture_writer_audio(CcCaptureWriter *writer, const float *stereo,
                             size_t frame_count);
const char *cc_capture_writer_error(const CcCaptureWriter *writer);

/* Finalizes the complete prefix even after a rejected append, then destroys writer. */
bool cc_capture_writer_close(CcCaptureWriter *writer);

#endif
