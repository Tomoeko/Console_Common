#include "console_common/platform/audio.h"

#include "console_common/support/error.h"

#include <float.h>

_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && sizeof(float) == 4,
               "Audio stream requires binary32 float");

bool cc_audio_description_validate(const CcAudioDescription *description,
                                   size_t *byte_count, char *error,
                                   size_t error_capacity) {
    if (!description || !byte_count || !description->sample_rate ||
        description->sample_rate > 384000 || !description->channels ||
        description->channels > 32 || !description->frames_per_buffer ||
        description->frames_per_buffer > 8192 || !description->buffer_count ||
        description->buffer_count > 64) {
        cc_error_set(error, error_capacity, "Unsupported audio stream dimensions.");
        return false;
    }
    *byte_count =
        (size_t)description->frames_per_buffer * description->channels * sizeof(float);
    return true;
}
