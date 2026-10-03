#include "output_internal.h"

CcAudioOutput *cc_audio_output_platform_open(const CcAudioOutputOptions *options) {
    (void)options;
    return NULL;
}

bool cc_audio_output_start(CcAudioOutput *output) {
    (void)output;
    return false;
}

bool cc_audio_output_stop(CcAudioOutput *output) {
    (void)output;
    return true;
}

bool cc_audio_output_failed(const CcAudioOutput *output) {
    (void)output;
    return false;
}

void cc_audio_output_close(CcAudioOutput *output) {
    (void)output;
}
