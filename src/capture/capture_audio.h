#ifndef CC_CAPTURE_AUDIO_H
#define CC_CAPTURE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcCaptureAudio CcCaptureAudio;

typedef struct {
    uint8_t decoder_config[2];
    uint32_t bitrate;
    uint32_t packet_frames;
    uint32_t priming_frames;
} CcCaptureAudioConfig;

typedef bool (*CcCaptureAudioPacket)(void *context, const uint8_t *bytes, size_t size,
                                     uint32_t duration_frames);

/* AAC storage and converter state are reusable and owned by the encoder.
 * Serialized calls run on the writer thread. Emit borrows one raw AAC access
 * unit until return; finish drains the final partial input and encoder delay.
 * Query configuration again after finish for the final reported priming count. */
bool cc_capture_audio_available(void);
CcCaptureAudio *cc_capture_audio_open(uint32_t sample_rate);
bool cc_capture_audio_append(CcCaptureAudio *audio, const float *stereo,
                             size_t frame_count, CcCaptureAudioPacket emit,
                             void *context);
bool cc_capture_audio_finish(CcCaptureAudio *audio, CcCaptureAudioPacket emit,
                             void *context);
bool cc_capture_audio_config(const CcCaptureAudio *audio, CcCaptureAudioConfig *config);
const char *cc_capture_audio_error(const CcCaptureAudio *audio);
void cc_capture_audio_close(CcCaptureAudio *audio);

#endif
