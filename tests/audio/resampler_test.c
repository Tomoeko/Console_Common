#include "console_common/audio/resampler.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double circle = 6.283185307179586476925286766559;

typedef struct {
    const float *samples;
    size_t count;
    size_t position;
    size_t calls;
    double rate;
    double frequency;
    bool infinite;
    bool constant;
} Source;

static bool source_frame(void *context, float stereo[2]) {
    Source *source = context;
    ++source->calls;
    if (!source->infinite && source->position == source->count)
        return false;
    if (source->samples) {
        memcpy(stereo, source->samples + source->position * 2, sizeof(float[2]));
    } else if (source->constant) {
        stereo[0] = 0.75f;
        stereo[1] = -0.5f;
    } else {
        double value =
            sin(circle * source->frequency * (double)source->position / source->rate);
        stereo[0] = (float)lrint(value * 12000) / 32768;
        stereo[1] = -stereo[0];
    }
    ++source->position;
    return true;
}

static void invalid_arguments(void) {
    assert(!cc_audio_resampler_create(0, 1, 48000));
    assert(!cc_audio_resampler_create(32000, 0, 48000));
    assert(!cc_audio_resampler_create(7999, 1, 48000));
    assert(!cc_audio_resampler_create(192001, 1, 48000));
    assert(!cc_audio_resampler_create(32000, 1, 7999));
    assert(!cc_audio_resampler_create(32000, 1, 192001));
    assert(!cc_audio_resampler_create(192000, 1, 8000));
    assert(!cc_audio_resample_state_create(1537));
    assert(cc_audio_resampler_taps(NULL) == 0);
    assert(cc_audio_resampler_delay_seconds(NULL) == 0);
    cc_audio_resampler_destroy(NULL);
    cc_audio_resample_state_destroy(NULL);
    cc_audio_resample_state_reset(NULL);
    float stereo[2] = {1, 1};
    assert(!cc_audio_resampler_frame(NULL, NULL, NULL, NULL, stereo));
    assert(stereo[0] == 0 && stereo[1] == 0);
    assert(!cc_audio_resampler_frame(NULL, NULL, NULL, NULL, NULL));
    stereo[0] = 1;
    assert(cc_audio_resampler_render(NULL, NULL, NULL, NULL, stereo, SIZE_MAX) == 0);
    assert(stereo[0] == 1);
}

static void passthrough_and_empty(void) {
    const uint32_t bits[] = {0x3e800000, 0xbf000000, 0x80000000, 0,
                             0x3f800000, 0xbf800000, 0x7fc00001, 0x00800000};
    float samples[8];
    memcpy(samples, bits, sizeof(samples));
    CcAudioResampler *profile = cc_audio_resampler_create(64000, 2, 32000);
    CcAudioResampleState *state = cc_audio_resample_state_create(0);
    assert(profile && state && cc_audio_resampler_taps(profile) == 0);
    assert(cc_audio_resampler_delay_seconds(profile) == 0);
    Source source = {.samples = samples, .count = 4};
    float output[12];
    assert(cc_audio_resampler_render(profile, state, source_frame, &source, output,
                                     6) == 4);
    assert(!memcmp(output, samples, sizeof(samples)));
    assert(output[8] == 0 && output[11] == 0);
    assert(!cc_audio_resampler_frame(profile, state, source_frame, &source, output));
    cc_audio_resample_state_reset(state);
    source = (Source){0};
    assert(cc_audio_resampler_render(profile, state, source_frame, &source, output,
                                     6) == 0);
    cc_audio_resample_state_destroy(state);
    cc_audio_resampler_destroy(profile);
}

static void chunk_identity_and_reset(unsigned output_rate) {
    const size_t source_count = 503;
    float samples[source_count * 2];
    for (size_t index = 0; index < source_count * 2; ++index)
        samples[index] = (float)((int)(index * 7919 % 60001) - 30000) / 32768;
    CcAudioResampler *profile = cc_audio_resampler_create(64057, 2, output_rate);
    CcAudioResampleState *whole =
        cc_audio_resample_state_create(cc_audio_resampler_taps(profile));
    CcAudioResampleState *chunks =
        cc_audio_resample_state_create(cc_audio_resampler_taps(profile));
    assert(profile && whole && chunks);
    float expected[16384] = {0};
    float actual[16384] = {0};
    Source first = {.samples = samples, .count = source_count};
    Source second = first;
    size_t expected_count =
        cc_audio_resampler_render(profile, whole, source_frame, &first, expected, 8192);
    size_t actual_count = 0;
    for (size_t request = 1; actual_count < 8192; request = request * 7 % 997 + 1) {
        if (request > 8192 - actual_count)
            request = 8192 - actual_count;
        size_t count = cc_audio_resampler_render(profile, chunks, source_frame, &second,
                                                 actual + actual_count * 2, request);
        actual_count += count;
        if (count < request)
            break;
    }
    assert(actual_count == expected_count && first.position == source_count &&
           second.position == source_count);
    assert(!memcmp(actual, expected, sizeof(expected)));
    uint64_t duration = (source_count + cc_audio_resampler_taps(profile) - 1) *
                        (uint64_t)output_rate * 2;
    assert(expected_count == (duration + 64056) / 64057);
    assert(first.calls == source_count + 1 && second.calls == source_count + 1);
    cc_audio_resample_state_reset(chunks);
    second.position = 0;
    second.calls = 0;
    assert(cc_audio_resampler_render(profile, chunks, source_frame, &second, actual,
                                     8192) == expected_count);
    assert(!memcmp(actual, expected, sizeof(expected)));
    cc_audio_resample_state_destroy(chunks);
    cc_audio_resample_state_destroy(whole);
    cc_audio_resampler_destroy(profile);
}

static void dc_impulse_and_clock(void) {
    CcAudioResampler *profile = cc_audio_resampler_create(64057, 2, 48000);
    CcAudioResampleState *state = cc_audio_resample_state_create(192);
    assert(profile && state && cc_audio_resampler_taps(profile) == 192);
    assert(fabs(cc_audio_resampler_delay_seconds(profile) - 96 / 32028.5) < 1e-12);
    Source source = {.infinite = true, .constant = true};
    float output[2048];
    assert(cc_audio_resampler_render(profile, state, source_frame, &source, output,
                                     1024) == 1024);
    assert(source.position == 1 + 1024u * 64057u / 96000u);
    for (size_t frame = 300; frame < 1024; ++frame) {
        assert(fabsf(output[frame * 2] - 0.75f) < 1e-6f);
        assert(fabsf(output[frame * 2 + 1] + 0.5f) < 1e-6f);
    }
    CcAudioResampler *impulse_profile = cc_audio_resampler_create(32000, 1, 48000);
    float impulse[128 * 2] = {1, -1};
    source = (Source){.samples = impulse, .count = 128};
    cc_audio_resample_state_reset(state);
    size_t count = cc_audio_resampler_render(impulse_profile, state, source_frame,
                                             &source, output, 1024);
    size_t maximum = 0;
    for (size_t frame = 1; frame < count; ++frame)
        if (fabsf(output[frame * 2]) > fabsf(output[maximum * 2]))
            maximum = frame;
    assert(maximum == 144 && output[288] > 0.9f);
    for (size_t frame = 0; frame < count; ++frame)
        assert(output[frame * 2] == -output[frame * 2 + 1]);
    assert(!cc_audio_resampler_frame(profile, state, source_frame, &source, output));
    cc_audio_resample_state_reset(state);
    source = (Source){0};
    assert(cc_audio_resampler_render(profile, state, source_frame, &source, output,
                                     1024) == 0);
    cc_audio_resampler_destroy(impulse_profile);
    cc_audio_resample_state_destroy(state);
    cc_audio_resampler_destroy(profile);
}

static void maximum_decimation_and_capacity(void) {
    CcAudioResampler *profile = cc_audio_resampler_create(64000, 1, 8000);
    CcAudioResampleState *too_small = cc_audio_resample_state_create(64);
    CcAudioResampleState *state = cc_audio_resample_state_create(1536);
    assert(profile && state && too_small && cc_audio_resampler_taps(profile) == 1536);
    Source source = {.infinite = true, .constant = true};
    float output[2];
    assert(
        !cc_audio_resampler_frame(profile, too_small, source_frame, &source, output));
    assert(source.position == 0 && output[0] == 0 && output[1] == 0);
    for (unsigned frame = 0; frame < 1000; ++frame) {
        assert(cc_audio_resampler_frame(profile, state, source_frame, &source, output));
        if (frame > 192) {
            assert(fabsf(output[0] - 0.75f) < 1e-6f);
            assert(fabsf(output[1] + 0.5f) < 1e-6f);
        }
    }
    assert(source.position == 8001);
    cc_audio_resample_state_reset(state);
    float samples[2] = {1, -1};
    source = (Source){.samples = samples, .count = 1};
    size_t drained = 0;
    while (cc_audio_resampler_frame(profile, state, source_frame, &source, output))
        ++drained;
    assert(drained == 192 && source.position == 1 && source.calls == 2);
    for (unsigned repeat = 0; repeat < 10; ++repeat)
        assert(
            !cc_audio_resampler_frame(profile, state, source_frame, &source, output));
    assert(source.calls == 2);
    cc_audio_resample_state_destroy(too_small);
    cc_audio_resample_state_destroy(state);
    cc_audio_resampler_destroy(profile);
}

static void short_filtered_source(uint32_t numerator, uint32_t denominator,
                                  unsigned output_rate, size_t frames) {
    CcAudioResampler *profile =
        cc_audio_resampler_create(numerator, denominator, output_rate);
    CcAudioResampleState *state =
        cc_audio_resample_state_create(cc_audio_resampler_taps(profile));
    assert(profile && state);
    Source source = {.count = frames, .constant = true};
    uint64_t duration = (frames + cc_audio_resampler_taps(profile) - 1) *
                        (uint64_t)denominator * output_rate;
    size_t expected = frames ? (size_t)((duration + numerator - 1) / numerator) : 0;
    float stereo[2];
    size_t count = 0;
    while (cc_audio_resampler_frame(profile, state, source_frame, &source, stereo))
        ++count;
    assert(count == expected && source.position == frames &&
           source.calls == frames + 1);
    for (unsigned repeat = 0; repeat < 7; ++repeat)
        assert(
            !cc_audio_resampler_frame(profile, state, source_frame, &source, stereo));
    assert(source.calls == frames + 1);
    cc_audio_resample_state_destroy(state);
    cc_audio_resampler_destroy(profile);
}

static void rejected_profile_preserves_stream(void) {
    CcAudioResampler *profile = cc_audio_resampler_create(32000, 1, 48000);
    CcAudioResampler *other = cc_audio_resampler_create(64057, 2, 48000);
    CcAudioResampler *larger = cc_audio_resampler_create(192000, 1, 24000);
    CcAudioResampleState *first = cc_audio_resample_state_create(192);
    CcAudioResampleState *second = cc_audio_resample_state_create(192);
    assert(profile && other && larger && first && second);
    Source source = {.rate = 32000, .frequency = 13000, .infinite = true};
    Source reference = source;
    float actual[2], expected[2];
    for (size_t frame = 0; frame < 97; ++frame) {
        assert(cc_audio_resampler_frame(profile, first, source_frame, &source, actual));
        assert(cc_audio_resampler_frame(profile, second, source_frame, &reference,
                                        expected));
        assert(!memcmp(actual, expected, sizeof(actual)));
    }
    size_t calls = source.calls;
    assert(!cc_audio_resampler_frame(other, first, source_frame, &source, actual));
    assert(!cc_audio_resampler_frame(larger, first, source_frame, &source, actual));
    assert(source.calls == calls && actual[0] == 0 && actual[1] == 0);
    for (size_t frame = 0; frame < 197; ++frame) {
        assert(cc_audio_resampler_frame(profile, first, source_frame, &source, actual));
        assert(cc_audio_resampler_frame(profile, second, source_frame, &reference,
                                        expected));
        assert(!memcmp(actual, expected, sizeof(actual)));
    }
    cc_audio_resample_state_destroy(second);
    cc_audio_resample_state_destroy(first);
    cc_audio_resampler_destroy(larger);
    cc_audio_resampler_destroy(other);
    cc_audio_resampler_destroy(profile);
}

static double tone_amplitude(const float *samples, unsigned rate, double frequency,
                             size_t first, size_t count) {
    double sine = 0, cosine = 0;
    for (size_t frame = first; frame < first + count; ++frame) {
        double phase = circle * frequency * (double)frame / rate;
        sine += samples[frame * 2] * sin(phase);
        cosine += samples[frame * 2] * cos(phase);
    }
    return hypot(sine, cosine) * 2 / (double)count / (12000.0 / 32768);
}

static void spectral_reconstruction(uint32_t numerator, uint32_t denominator,
                                    unsigned output_rate, double frequency,
                                    double unwanted_frequency, double gain_limit,
                                    double unwanted_limit) {
    CcAudioResampler *profile =
        cc_audio_resampler_create(numerator, denominator, output_rate);
    CcAudioResampleState *state =
        cc_audio_resample_state_create(cc_audio_resampler_taps(profile));
    assert(profile && state);
    size_t count = (size_t)output_rate * 3;
    float *samples = calloc(count * 2, sizeof(float));
    assert(samples);
    Source source = {.rate = (double)numerator / denominator,
                     .frequency = frequency,
                     .infinite = true};
    assert(cc_audio_resampler_render(profile, state, source_frame, &source, samples,
                                     count) == count);
    size_t measured = (size_t)output_rate * 2;
    double gain =
        tone_amplitude(samples, output_rate, frequency, output_rate, measured);
    if (frequency < (double)output_rate / 2)
        assert(fabs(20 * log10(gain)) < gain_limit);
    double unwanted =
        tone_amplitude(samples, output_rate, unwanted_frequency, output_rate, measured);
    if (unwanted >= unwanted_limit)
        fprintf(stderr, "SRC %u/%u to %u, tone %.1f Hz: gain %.4f dB, image %.4f dB\n",
                numerator, denominator, output_rate, frequency, 20 * log10(gain),
                20 * log10(unwanted));
    assert(unwanted < unwanted_limit);
    free(samples);
    cc_audio_resample_state_destroy(state);
    cc_audio_resampler_destroy(profile);
}

int main(void) {
    invalid_arguments();
    passthrough_and_empty();
    const unsigned rates[] = {8000, 11025, 22050, 32000, 44100, 48000, 192000};
    for (size_t index = 0; index < sizeof(rates) / sizeof(rates[0]); ++index)
        chunk_identity_and_reset(rates[index]);
    dc_impulse_and_clock();
    maximum_decimation_and_capacity();
    const size_t short_counts[] = {0, 1, 2, 7, 31};
    for (size_t index = 0; index < sizeof(short_counts) / sizeof(short_counts[0]);
         ++index) {
        short_filtered_source(64057, 2, 48000, short_counts[index]);
        short_filtered_source(64057, 2, 8000, short_counts[index]);
        short_filtered_source(192000, 1, 24000, short_counts[index]);
    }
    rejected_profile_preserves_stream();
    const double frequencies[] = {1000, 4000, 8000, 12000, 14000, 14500, 15000, 15500};
    for (size_t index = 0; index < sizeof(frequencies) / sizeof(frequencies[0]);
         ++index) {
        double image = 32028.5 - frequencies[index];
        if (image > 24000)
            image = 48000 - image;
        spectral_reconstruction(64057, 2, 48000, frequencies[index], image, 0.1, 0.001);
        spectral_reconstruction(32000, 1, 48000, frequencies[index],
                                32000 - frequencies[index] > 24000
                                    ? 16000 + frequencies[index]
                                    : 32000 - frequencies[index],
                                0.1, 0.001);
    }
    /* A bounded filter has a finite transition at Nyquist. Check that it
     * improves both gain and the first image even within that transition.
     */
    spectral_reconstruction(64057, 2, 48000, 15900, 16128.5, 2.4, 0.25);
    spectral_reconstruction(32000, 1, 48000, 15900, 16100, 2.8, 0.27);
    spectral_reconstruction(64057, 2, 32000, 15500, 15471.5, 0.1, 0.001);
    spectral_reconstruction(64057, 2, 8000, 1000, 3028.5, 0.1, 0.001);
    spectral_reconstruction(64057, 2, 8000, 10000, 2000, 0.1, 0.001);
    puts("Band-limited host reconstruction tests passed.");
    return 0;
}
