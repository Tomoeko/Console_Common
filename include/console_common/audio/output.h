#ifndef CONSOLE_COMMON_AUDIO_OUTPUT_H
#define CONSOLE_COMMON_AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct CcAudioOutput CcAudioOutput;
typedef void (*CcAudioRender)(void *context, float *stereo, size_t frames);

typedef enum CcAudioOutputMode {
    CC_AUDIO_OUTPUT_BUFFERED,
    CC_AUDIO_OUTPUT_DIRECT
} CcAudioOutputMode;

typedef struct CcAudioOutputOptions {
    unsigned sample_rate;
    CcAudioRender render;
    void *context;
    CcAudioOutputMode mode;
} CcAudioOutputOptions;

/* Open stopped, at 8000-192000 Hz, with native interleaved stereo floats.
 * The borrowed context remains valid until stop or close returns. Serialize
 * all lifecycle calls on a control thread, never inside the renderer. The
 * renderer must overwrite every sample without allocation, I/O, or blocking.
 * Apple BUFFERED output primes three 512-frame buffers on each start; DIRECT
 * output receives the AudioUnit's variable callback sizes. Linux uses 512-frame
 * writes for both modes. Host conversion remains outside the renderer.
 */
CcAudioOutput *cc_audio_output_open(const CcAudioOutputOptions *options);
bool cc_audio_output_start(CcAudioOutput *output);
/* Stops rendering and discards pending device frames. Even a failed stop
 * quiesces calls to the borrowed renderer before returning. */
bool cc_audio_output_stop(CcAudioOutput *output);
/* Transport failures latch until close; null is not a failed output. */
bool cc_audio_output_failed(const CcAudioOutput *output);
void cc_audio_output_close(CcAudioOutput *output);

#endif
