#ifndef CONSOLE_COMMON_AUDIO_OUTPUT_INTERNAL_H
#define CONSOLE_COMMON_AUDIO_OUTPUT_INTERNAL_H

#include "console_common/audio/output.h"

enum {
    CC_AUDIO_OUTPUT_FRAMES = 512,
    CC_AUDIO_OUTPUT_CHANNELS = 2,
    CC_AUDIO_OUTPUT_BUFFERS = 3
};

CcAudioOutput *cc_audio_output_platform_open(const CcAudioOutputOptions *options);

#endif
