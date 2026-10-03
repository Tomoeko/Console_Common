#define _POSIX_C_SOURCE 200809L

#include "console_common/audio/output.h"

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef ESTRPIPE
/* Hosts without ALSA still exercise the Linux suspend error in the mocks. */
#define ESTRPIPE 86
#endif

/* Runtime symbol mocks test the ALSA route on any Unix host. No ALSA headers,
 * library, sound server, or hardware are needed for these delivery checks. */
typedef struct {
    unsigned sample_rate;
    unsigned library_identity;
    unsigned pcm_identity;
    unsigned thread_identity;
    unsigned symbols;
    unsigned library_closes;
    unsigned pcm_closes;
    unsigned prepares;
    unsigned drops;
    unsigned creates;
    unsigned joins;
    unsigned rendered;
    unsigned recoveries;
    unsigned sleeps;
    unsigned write_calls;
    unsigned write_count;
    unsigned long source_position;
    long writes[8];
    unsigned fail_symbol;
    unsigned fail_prepare;
    unsigned stop_write;
    unsigned stop_sleep;
    bool fail_owner;
    bool fail_library;
    bool fail_format;
    bool fail_open;
    bool fail_parameters;
    bool fail_create;
    bool fail_join;
    bool fail_drop;
    bool fail_recover;
    bool stop_render;
    void *(*worker)(void *);
    void *worker_context;
} OutputFixture;

static OutputFixture fixture;
static void stop_worker(void);

static void *output_allocate(size_t count, size_t size) {
    return fixture.fail_owner ? NULL : calloc(count, size);
}

static void *library_open(const char *name, int flags) {
    assert(strcmp(name, "libasound.so.2") == 0);
    assert(flags == (RTLD_NOW | RTLD_LOCAL));
    return fixture.fail_library ? NULL : &fixture.library_identity;
}

static int library_close(void *library) {
    assert(library == &fixture.library_identity);
    ++fixture.library_closes;
    return 0;
}

static int pcm_open(void **pcm, const char *name, int stream, int flags) {
    assert(strcmp(name, "default") == 0 && stream == 0);
    assert(flags == 1); /* Nonblocking output must remain stoppable on EAGAIN. */
    if (fixture.fail_open)
        return -1;
    *pcm = &fixture.pcm_identity;
    return 0;
}

static int pcm_close(void *pcm) {
    assert(pcm == &fixture.pcm_identity);
    ++fixture.pcm_closes;
    return 0;
}

static int pcm_drop(void *pcm) {
    assert(pcm == &fixture.pcm_identity);
    ++fixture.drops;
    return fixture.fail_drop ? -1 : 0;
}

static int pcm_prepare(void *pcm) {
    assert(pcm == &fixture.pcm_identity);
    ++fixture.prepares;
    return fixture.prepares == fixture.fail_prepare ? -1 : 0;
}

static int pcm_parameters(void *pcm, int format, int access, unsigned channels,
                          unsigned rate, int resample, unsigned latency) {
    assert(pcm == &fixture.pcm_identity && format == 14 && access == 3);
    assert(channels == 2 && rate == fixture.sample_rate && resample == 1);
    assert(latency == 80000);
    return fixture.fail_parameters ? -1 : 0;
}

static long pcm_write(void *pcm, const void *buffer, unsigned long frames) {
    assert(pcm == &fixture.pcm_identity && fixture.write_calls < fixture.write_count);
    assert(frames == 512 - fixture.source_position);
    const float *samples = buffer;
    for (unsigned long index = 0; index < frames * 2; ++index)
        assert(samples[index] == (float)(fixture.source_position * 2 + index));
    long written = fixture.writes[fixture.write_calls++];
    if (written > 0 && (unsigned long)written <= frames)
        fixture.source_position += (unsigned long)written;
    if (fixture.write_calls == fixture.stop_write)
        stop_worker();
    return written;
}

static int pcm_recover(void *pcm, int error, int silent) {
    assert(pcm == &fixture.pcm_identity && (error == -EPIPE || error == -EINTR));
    assert(silent == 1);
    ++fixture.recoveries;
    return fixture.fail_recover ? -1 : 0;
}

static int pcm_format(const char *name) {
    uint16_t endian = 1;
    const char *native_name = *(const uint8_t *)&endian ? "FLOAT_LE" : "FLOAT_BE";
    assert(strcmp(name, native_name) == 0);
    return fixture.fail_format ? -1 : 14;
}

static void *function_address(const void *function, size_t size) {
    void *address;
    assert(size == sizeof(address));
    memcpy(&address, function, sizeof(address));
    return address;
}

static void *library_symbol(void *library, const char *name) {
    assert(library == &fixture.library_identity);
    if (++fixture.symbols == fixture.fail_symbol)
        return NULL;
    int (*open_function)(void **, const char *, int, int) = pcm_open;
    int (*close_function)(void *) = pcm_close;
    int (*drop_function)(void *) = pcm_drop;
    int (*prepare_function)(void *) = pcm_prepare;
    int (*parameters_function)(void *, int, int, unsigned, unsigned, int, unsigned) =
        pcm_parameters;
    long (*write_function)(void *, const void *, unsigned long) = pcm_write;
    int (*recover_function)(void *, int, int) = pcm_recover;
    int (*format_function)(const char *) = pcm_format;
    if (strcmp(name, "snd_pcm_open") == 0)
        return function_address(&open_function, sizeof(open_function));
    if (strcmp(name, "snd_pcm_close") == 0)
        return function_address(&close_function, sizeof(close_function));
    if (strcmp(name, "snd_pcm_drop") == 0)
        return function_address(&drop_function, sizeof(drop_function));
    if (strcmp(name, "snd_pcm_prepare") == 0)
        return function_address(&prepare_function, sizeof(prepare_function));
    if (strcmp(name, "snd_pcm_set_params") == 0)
        return function_address(&parameters_function, sizeof(parameters_function));
    if (strcmp(name, "snd_pcm_writei") == 0)
        return function_address(&write_function, sizeof(write_function));
    if (strcmp(name, "snd_pcm_recover") == 0)
        return function_address(&recover_function, sizeof(recover_function));
    assert(strcmp(name, "snd_pcm_format_value") == 0);
    return function_address(&format_function, sizeof(format_function));
}

static int worker_create(pthread_t *thread, const pthread_attr_t *attributes,
                         void *(*worker)(void *), void *context) {
    assert(!attributes);
    ++fixture.creates;
    if (fixture.fail_create)
        return 1;
    *thread = (pthread_t)(uintptr_t)&fixture.thread_identity;
    fixture.worker = worker;
    fixture.worker_context = context;
    return 0;
}

static int worker_join(pthread_t thread, void **result) {
    assert(thread == (pthread_t)(uintptr_t)&fixture.thread_identity && !result);
    ++fixture.joins;
    return fixture.fail_join ? 1 : 0;
}

static int worker_pause(const struct timespec *pause, struct timespec *remaining) {
    assert(!remaining && pause->tv_sec == 0 && pause->tv_nsec == 1000000);
    ++fixture.sleeps;
    if (fixture.sleeps == fixture.stop_sleep)
        stop_worker();
    return 0;
}

static void render_samples(void *context, float *stereo, size_t frames) {
    assert(context == &fixture && frames == 512);
    fixture.rendered += (unsigned)frames;
    for (size_t index = 0; index < frames * 2; ++index)
        stereo[index] = (float)index;
    if (fixture.stop_render)
        stop_worker();
}

#define calloc output_allocate
#define dlopen library_open
#define dlclose library_close
#define dlsym library_symbol
#define pthread_create worker_create
#define pthread_join worker_join
#define nanosleep worker_pause
#include "../../src/audio/output.c"
#include "../../src/platform/linux/audio_output_linux.c"
#undef calloc

static void stop_worker(void) {
    CcAudioOutput *output = fixture.worker_context;
    atomic_store(&output->running, false);
}

static void reset_fixture(void) {
    fixture = (OutputFixture){.sample_rate = 48000};
}

static CcAudioOutput *open_output(CcAudioOutputMode mode) {
    CcAudioOutputOptions options = {.sample_rate = fixture.sample_rate,
                                    .render = render_samples,
                                    .context = &fixture,
                                    .mode = mode};
    return cc_audio_output_open(&options);
}

static void run_worker(void) {
    assert(fixture.worker && fixture.worker_context);
    assert(fixture.worker(fixture.worker_context) == NULL);
}

static void test_open_failures(void) {
    for (unsigned failure = 0; failure < 13; ++failure) {
        reset_fixture();
        if (failure == 0)
            fixture.fail_owner = true;
        else if (failure == 1)
            fixture.fail_library = true;
        else if (failure < 10)
            fixture.fail_symbol = failure - 1;
        else if (failure == 10)
            fixture.fail_format = true;
        else if (failure == 11)
            fixture.fail_open = true;
        else
            fixture.fail_parameters = true;
        assert(!open_output(CC_AUDIO_OUTPUT_DIRECT));
        assert(!fixture.rendered && !fixture.creates && !fixture.prepares);
        assert(fixture.library_closes == (failure > 1 ? 1u : 0u));
        assert(fixture.pcm_closes == (failure == 12 ? 1u : 0u));
    }
}

static void test_start_failures(void) {
    for (unsigned failure = 0; failure < 2; ++failure) {
        reset_fixture();
        CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
        assert(output && !fixture.rendered && !fixture.creates);
        if (!failure)
            fixture.fail_prepare = 1;
        else
            fixture.fail_create = true;
        assert(!cc_audio_output_start(output) && cc_audio_output_failed(output));
        assert(!cc_audio_output_start(output));
        cc_audio_output_close(output);
        assert(!fixture.rendered && !fixture.joins);
        assert(fixture.pcm_closes == 1 && fixture.library_closes == 1);
    }
}

static void test_delivery(CcAudioOutputMode mode, unsigned rate) {
    reset_fixture();
    fixture.sample_rate = rate;
    CcAudioOutput *output = open_output(mode);
    assert(output && !fixture.rendered && !fixture.creates);
    assert(cc_audio_output_start(output) && cc_audio_output_start(output));
    assert(fixture.creates == 1 && fixture.prepares == 1);
    fixture.writes[0] = 100;
    fixture.writes[1] = -EAGAIN;
    fixture.writes[2] = 0;
    fixture.writes[3] = -EPIPE;
    fixture.writes[4] = -EINTR;
    fixture.writes[5] = 412;
    fixture.write_count = fixture.stop_write = 6;
    run_worker();
    assert(fixture.rendered == 512 && fixture.source_position == 512);
    assert(fixture.recoveries == 2 && fixture.sleeps == 2);
    assert(!cc_audio_output_failed(output));
    assert(cc_audio_output_stop(output));
    assert(fixture.joins == 1 && fixture.drops == 1);
    assert(cc_audio_output_stop(output) && fixture.joins == 1);
    assert(cc_audio_output_start(output));
    assert(fixture.creates == 2 && fixture.prepares == 2);
    cc_audio_output_close(output);
    assert(fixture.joins == 2 && fixture.drops == 2);
    assert(fixture.pcm_closes == 1 && fixture.library_closes == 1);
}

static void test_stop_on_retry(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.writes[0] = fixture.writes[1] = fixture.writes[2] = -EAGAIN;
    fixture.write_count = fixture.stop_sleep = 3;
    run_worker();
    assert(fixture.rendered == 512 && fixture.write_calls == 3 && fixture.sleeps == 3);
    assert(!fixture.recoveries && !cc_audio_output_failed(output));
    cc_audio_output_close(output);
    assert(fixture.joins == 1 && fixture.drops == 1);
}

static void test_stop_before_recovery(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.writes[0] = -EPIPE;
    fixture.write_count = fixture.stop_write = 1;
    run_worker();
    assert(fixture.rendered == 512 && !fixture.recoveries);
    assert(!cc_audio_output_failed(output));
    cc_audio_output_close(output);
}

static void test_stop_before_write(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.stop_render = true;
    run_worker();
    assert(fixture.rendered == 512 && !fixture.write_calls && !fixture.recoveries);
    cc_audio_output_close(output);
}

static void test_suspend_restart(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.writes[0] = 100;
    fixture.writes[1] = -ESTRPIPE;
    fixture.writes[2] = 412;
    fixture.write_count = fixture.stop_write = 3;
    run_worker();
    assert(fixture.prepares == 2 && !fixture.recoveries);
    assert(fixture.source_position == 512 && !cc_audio_output_failed(output));
    cc_audio_output_close(output);
}

static void test_worker_failures(void) {
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset_fixture();
        CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
        assert(output && cc_audio_output_start(output));
        fixture.write_count = 1;
        if (!failure) {
            fixture.writes[0] = -EPIPE;
            fixture.fail_recover = true;
        } else if (failure == 1) {
            fixture.writes[0] = -ENODEV;
        } else {
            fixture.writes[0] = 513;
        }
        run_worker();
        assert(fixture.rendered == 512 && cc_audio_output_failed(output));
        assert(!cc_audio_output_start(output));
        cc_audio_output_close(output);
        assert(fixture.joins == 1 && fixture.drops == 1);
        assert(fixture.pcm_closes == 1 && fixture.library_closes == 1);
    }
}

static void test_stop_failures(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.fail_drop = true;
    assert(!cc_audio_output_stop(output) && cc_audio_output_failed(output));
    assert(!cc_audio_output_start(output));
    cc_audio_output_close(output);
    assert(fixture.joins == 1 && fixture.pcm_closes == 1);

    reset_fixture();
    output = open_output(CC_AUDIO_OUTPUT_DIRECT);
    assert(output && cc_audio_output_start(output));
    fixture.fail_join = true;
    assert(!cc_audio_output_stop(output) && cc_audio_output_failed(output));
    cc_audio_output_close(output);
    assert(fixture.pcm_closes == 0 && fixture.library_closes == 0);
    fixture.fail_join = false;
    cc_audio_output_close(output);
    assert(fixture.pcm_closes == 1 && fixture.library_closes == 1);
}

int main(void) {
    test_open_failures();
    test_start_failures();
    test_delivery(CC_AUDIO_OUTPUT_BUFFERED, 8000);
    test_delivery(CC_AUDIO_OUTPUT_DIRECT, 48000);
    test_delivery(CC_AUDIO_OUTPUT_DIRECT, 192000);
    test_stop_on_retry();
    test_stop_before_recovery();
    test_stop_before_write();
    test_suspend_restart();
    test_worker_failures();
    test_stop_failures();
    assert(!cc_audio_output_start(NULL) && cc_audio_output_stop(NULL));
    assert(!cc_audio_output_failed(NULL));
    cc_audio_output_close(NULL);
    puts("Shared Linux audio output tests passed.");
    return 0;
}
