#ifndef CONSOLE_COMMON_AUDIO_RESAMPLER_H
#define CONSOLE_COMMON_AUDIO_RESAMPLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcAudioResampler CcAudioResampler;
typedef struct CcAudioResampleState CcAudioResampleState;
typedef bool (*CcAudioSourceFrame)(void *context, float stereo[2]);

/* Source rates are rational so a fractional native DAC clock is retained.
 * Supported rates are 8000-192000 Hz; decimation is bounded to an 8:1 ratio.
 * Filter storage is bounded to 1536 taps, with 192 taps for upsampling.
 * Coefficients belong to this immutable profile and are computed only here,
 * outside the audio callback. Same-rate conversion is an exact passthrough.
 */
CcAudioResampler *cc_audio_resampler_create(uint32_t source_numerator,
                                            uint32_t source_denominator,
                                            unsigned output_rate);
void cc_audio_resampler_destroy(CcAudioResampler *resampler);
size_t cc_audio_resampler_taps(const CcAudioResampler *resampler);
double cc_audio_resampler_delay_seconds(const CcAudioResampler *resampler);

/* Allocate one state per playback stream before rendering. Profiles can be
 * shared, but state must have capacity for its profile's taps. Reset when a
 * different stream/profile takes ownership. No callback allocation occurs.
 */
CcAudioResampleState *cc_audio_resample_state_create(size_t maximum_taps);
void cc_audio_resample_state_destroy(CcAudioResampleState *state);
void cc_audio_resample_state_reset(CcAudioResampleState *state);

/* The source callback returns false at EOF. The converter drains its finite
 * filter tail before returning false/short output; missing history starts at
 * zero. The causal filter delay is half its tap count in source frames and
 * applies equally to live output and recordings. Native DSP arithmetic stays
 * outside this host reconstruction step. Serialize state/profile lifetimes.
 */
bool cc_audio_resampler_frame(const CcAudioResampler *resampler,
                              CcAudioResampleState *state, CcAudioSourceFrame source,
                              void *context, float stereo[2]);
size_t cc_audio_resampler_render(const CcAudioResampler *resampler,
                                 CcAudioResampleState *state, CcAudioSourceFrame source,
                                 void *context, float *stereo, size_t frames);

#endif
