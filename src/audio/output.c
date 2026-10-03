#include "output_internal.h"

CcAudioOutput *cc_audio_output_open(const CcAudioOutputOptions *options) {
    if (!options || !options->render || options->sample_rate < 8000 ||
        options->sample_rate > 192000 ||
        (options->mode != CC_AUDIO_OUTPUT_BUFFERED &&
         options->mode != CC_AUDIO_OUTPUT_DIRECT))
        return NULL;
    return cc_audio_output_platform_open(options);
}
