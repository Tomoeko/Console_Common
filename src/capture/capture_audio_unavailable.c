#include "capture_audio.h"

bool cc_capture_audio_available(void) {
    return false;
}

CcCaptureAudio *cc_capture_audio_open(uint32_t sample_rate) {
    (void)sample_rate;
    return NULL;
}

bool cc_capture_audio_append(CcCaptureAudio *audio, const float *stereo,
                             size_t frame_count, CcCaptureAudioPacket emit,
                             void *context) {
    (void)audio;
    (void)stereo;
    (void)frame_count;
    (void)emit;
    (void)context;
    return false;
}

bool cc_capture_audio_finish(CcCaptureAudio *audio, CcCaptureAudioPacket emit,
                             void *context) {
    (void)audio;
    (void)emit;
    (void)context;
    return false;
}

bool cc_capture_audio_config(const CcCaptureAudio *audio,
                             CcCaptureAudioConfig *config) {
    (void)audio;
    (void)config;
    return false;
}

const char *cc_capture_audio_error(const CcCaptureAudio *audio) {
    (void)audio;
    return "Web recording requires a system AAC encoder.";
}

void cc_capture_audio_close(CcCaptureAudio *audio) {
    (void)audio;
}
