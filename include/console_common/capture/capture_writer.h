#ifndef CC_CAPTURE_WRITER_H
#define CC_CAPTURE_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcCaptureWriter CcCaptureWriter;

typedef enum { CC_CAPTURE_AUDIO_NORMAL, CC_CAPTURE_AUDIO_WEB } CcCaptureAudioMode;

/* Web recording requires system AAC and H.264 encoders. Normal is portable. */
bool cc_capture_audio_mode_supported(CcCaptureAudioMode mode);

/* Calls are serialized by the owner. Opening never overwrites an existing file. */
CcCaptureWriter *cc_capture_writer_open(const char *path, unsigned width,
                                        unsigned height, uint32_t video_timescale,
                                        uint32_t audio_rate);
CcCaptureWriter *cc_capture_writer_open_with_audio(const char *path, unsigned width,
                                                   unsigned height,
                                                   uint32_t video_timescale,
                                                   uint32_t audio_rate,
                                                   CcCaptureAudioMode audio_mode);

/* Video uses the platform's high-quality codec; the opaque track omits alpha.
 * Normal audio preserves every interleaved IEEE-754 binary32 sample word exactly.
 * Web audio uses AAC-LC; encoder delay and padding are trimmed by the MP4 track. */
bool cc_capture_writer_video(CcCaptureWriter *writer, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks);
bool cc_capture_writer_audio(CcCaptureWriter *writer, const float *stereo,
                             size_t frame_count);
const char *cc_capture_writer_error(const CcCaptureWriter *writer);

/* Finalizes the complete prefix even after a rejected append, then destroys writer. */
bool cc_capture_writer_close(CcCaptureWriter *writer);

#endif
