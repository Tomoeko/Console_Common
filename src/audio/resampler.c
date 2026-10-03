#include "console_common/audio/resampler.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { RESAMPLE_PHASES = 64, RESAMPLE_BASE_TAPS = 192, RESAMPLE_MAX_TAPS = 1536 };

struct CcAudioResampler {
    uint32_t numerator;
    uint64_t denominator;
    size_t taps;
    double delay_seconds;
    float coefficients[];
};

struct CcAudioResampleState {
    const CcAudioResampler *profile;
    uint64_t phase;
    size_t capacity;
    size_t cursor;
    size_t zero_frames;
    bool primed;
    bool source_ended;
    bool finished;
    float history[][2];
};

static uint32_t greatest_divisor(uint32_t first, uint32_t second) {
    while (second) {
        uint32_t remainder = first % second;
        first = second;
        second = remainder;
    }
    return first;
}

static double bessel_zero(double value) {
    double sum = 1;
    double term = 1;
    for (unsigned order = 1; order < 40; ++order) {
        term *= value * value / (4 * order * order);
        sum += term;
        if (term < sum * 1e-15)
            break;
    }
    return sum;
}

static void prepare_coefficients(CcAudioResampler *resampler, double ratio) {
    const double pi = 3.1415926535897932384626433832795;
    const double window_normalization = bessel_zero(8.6);
    /* Center the finite transition at the lower Nyquist frequency. Keeping
     * this at Nyquist also avoids discarding valid high source frequencies
     * when the host rate is only slightly below the native clock.
     */
    double cutoff = fmin(1, 1 / ratio);
    double radius = (double)resampler->taps / 2;
    for (unsigned phase = 0; phase <= RESAMPLE_PHASES; ++phase) {
        double fraction = (double)phase / RESAMPLE_PHASES;
        double sum = 0;
        float *coefficients = resampler->coefficients + phase * resampler->taps;
        for (size_t tap = 0; tap < resampler->taps; ++tap) {
            double distance = radius - (double)tap - fraction;
            double normalized_distance = distance / radius;
            double window =
                bessel_zero(8.6 * sqrt(fmax(0, 1 - normalized_distance *
                                                       normalized_distance))) /
                window_normalization;
            double argument = pi * distance * cutoff;
            double sinc = fabs(argument) < 1e-15 ? 1 : sin(argument) / argument;
            double coefficient = cutoff * sinc * window;
            coefficients[tap] = (float)coefficient;
            sum += coefficient;
        }
        for (size_t tap = 0; tap < resampler->taps; ++tap)
            coefficients[tap] /= (float)sum;
    }
}

CcAudioResampler *cc_audio_resampler_create(uint32_t source_numerator,
                                            uint32_t source_denominator,
                                            unsigned output_rate) {
    if (!source_denominator || output_rate < 8000 || output_rate > 192000)
        return NULL;
    double source_rate = (double)source_numerator / source_denominator;
    double ratio = source_rate / output_rate;
    if (source_rate < 8000 || source_rate > 192000 || ratio > 8)
        return NULL;
    uint32_t divisor = greatest_divisor(source_numerator, source_denominator);
    source_numerator /= divisor;
    source_denominator /= divisor;
    uint64_t denominator = (uint64_t)source_denominator * output_rate;
    size_t taps = source_numerator == denominator
                      ? 0
                      : (size_t)ceil(RESAMPLE_BASE_TAPS * fmax(1, ratio));
    if (taps & 1)
        ++taps;
    if (taps > RESAMPLE_MAX_TAPS)
        return NULL;
    size_t coefficient_count = (RESAMPLE_PHASES + 1) * taps;
    CcAudioResampler *resampler =
        calloc(1, sizeof(*resampler) + coefficient_count * sizeof(float));
    if (!resampler)
        return NULL;
    resampler->numerator = source_numerator;
    resampler->denominator = denominator;
    resampler->taps = taps;
    resampler->delay_seconds = (double)taps / (2 * source_rate);
    if (taps)
        prepare_coefficients(resampler, ratio);
    return resampler;
}

void cc_audio_resampler_destroy(CcAudioResampler *resampler) {
    free(resampler);
}

size_t cc_audio_resampler_taps(const CcAudioResampler *resampler) {
    return resampler ? resampler->taps : 0;
}

double cc_audio_resampler_delay_seconds(const CcAudioResampler *resampler) {
    return resampler ? resampler->delay_seconds : 0;
}

CcAudioResampleState *cc_audio_resample_state_create(size_t maximum_taps) {
    if (maximum_taps > RESAMPLE_MAX_TAPS)
        return NULL;
    size_t samples = maximum_taps * 2;
    CcAudioResampleState *state =
        calloc(1, sizeof(*state) + samples * sizeof(float[2]));
    if (state)
        state->capacity = maximum_taps;
    return state;
}

void cc_audio_resample_state_destroy(CcAudioResampleState *state) {
    free(state);
}

void cc_audio_resample_state_reset(CcAudioResampleState *state) {
    if (!state)
        return;
    size_t capacity = state->capacity;
    memset(state, 0, sizeof(*state) + capacity * 2 * sizeof(float[2]));
    state->capacity = capacity;
}

static void append_source(const CcAudioResampler *resampler,
                          CcAudioResampleState *state, CcAudioSourceFrame source,
                          void *context) {
    float stereo[2] = {0};
    if (!state->source_ended && !source(context, stereo)) {
        state->source_ended = true;
        stereo[0] = stereo[1] = 0;
    }
    if (state->source_ended)
        ++state->zero_frames;
    memcpy(state->history[state->cursor], stereo, sizeof(stereo));
    memcpy(state->history[state->cursor + resampler->taps], stereo, sizeof(stereo));
    if (state->zero_frames == resampler->taps)
        state->finished = true;
}

static bool render_frame(const CcAudioResampler *resampler, CcAudioResampleState *state,
                         CcAudioSourceFrame source, void *context, float stereo[2]) {
    if (state->finished)
        return false;
    if (!resampler->taps) {
        if (source(context, stereo))
            return true;
        state->finished = true;
        stereo[0] = stereo[1] = 0;
        return false;
    }
    if (!state->primed) {
        append_source(resampler, state, source, context);
        state->primed = true;
        if (state->source_ended) {
            state->finished = true;
            return false;
        }
    }
    uint64_t scaled_phase = state->phase * RESAMPLE_PHASES;
    size_t phase = (size_t)(scaled_phase / resampler->denominator);
    float fraction = (float)((double)(scaled_phase % resampler->denominator) /
                             (double)resampler->denominator);
    const float *first = resampler->coefficients + phase * resampler->taps;
    const float *second = first + resampler->taps;
    const float(*samples)[2] = state->history + state->cursor + resampler->taps;
    for (size_t tap = 0; tap < resampler->taps; ++tap) {
        float coefficient = first[tap] + (second[tap] - first[tap]) * fraction;
        stereo[0] += samples[-(ptrdiff_t)tap][0] * coefficient;
        stereo[1] += samples[-(ptrdiff_t)tap][1] * coefficient;
    }
    state->phase += resampler->numerator;
    while (state->phase >= resampler->denominator && !state->finished) {
        state->phase -= resampler->denominator;
        state->cursor = (state->cursor + 1) % resampler->taps;
        append_source(resampler, state, source, context);
    }
    return true;
}

static bool valid_stream(const CcAudioResampler *resampler, CcAudioResampleState *state,
                         CcAudioSourceFrame source) {
    if (!resampler || !state || !source || state->capacity < resampler->taps ||
        (state->profile && state->profile != resampler))
        return false;
    state->profile = resampler;
    return true;
}

bool cc_audio_resampler_frame(const CcAudioResampler *resampler,
                              CcAudioResampleState *state, CcAudioSourceFrame source,
                              void *context, float stereo[2]) {
    if (!stereo)
        return false;
    stereo[0] = stereo[1] = 0;
    return valid_stream(resampler, state, source) &&
           render_frame(resampler, state, source, context, stereo);
}

size_t cc_audio_resampler_render(const CcAudioResampler *resampler,
                                 CcAudioResampleState *state, CcAudioSourceFrame source,
                                 void *context, float *stereo, size_t frames) {
    if (!stereo || frames > SIZE_MAX / (2 * sizeof(float)))
        return 0;
    memset(stereo, 0, frames * 2 * sizeof(float));
    if (!valid_stream(resampler, state, source))
        return 0;
    size_t completed = 0;
    while (completed < frames &&
           render_frame(resampler, state, source, context, stereo + completed * 2))
        ++completed;
    return completed;
}
