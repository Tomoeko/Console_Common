#define _POSIX_C_SOURCE 200809L

#include "audio/output_internal.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    CC_ALSA_ACCESS_RW_INTERLEAVED = 3,
    CC_ALSA_OPEN_NONBLOCK = 1,
    CC_ALSA_BUFFER_MICROSECONDS = 80000
};

struct CcAudioOutput {
    CcAudioOutputOptions options;
    void *library;
    void *pcm;
    pthread_t thread;
    atomic_bool running;
    atomic_bool failed;
    atomic_uint callbacks;
    bool thread_started;
    int (*pcm_open)(void **, const char *, int, int);
    int (*pcm_close)(void *);
    int (*pcm_drop)(void *);
    int (*pcm_prepare)(void *);
    int (*pcm_set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
    long (*pcm_writei)(void *, const void *, unsigned long);
    int (*pcm_recover)(void *, int, int);
    int (*pcm_format_value)(const char *);
};

static bool load_symbol(void *library, const char *name, void *function,
                        size_t function_size) {
    void *symbol = dlsym(library, name);
    if (!symbol || function_size != sizeof(symbol))
        return false;
    memcpy(function, &symbol, sizeof(symbol));
    return true;
}

static void fail_output(CcAudioOutput *output) {
    atomic_store(&output->failed, true);
    atomic_store(&output->running, false);
}

static bool render_buffer(CcAudioOutput *output, float *buffer) {
    atomic_fetch_add(&output->callbacks, 1);
    bool running = atomic_load(&output->running);
    if (running)
        output->options.render(output->options.context, buffer, CC_AUDIO_OUTPUT_FRAMES);
    atomic_fetch_sub(&output->callbacks, 1);
    return running;
}

static void wait_writable(void) {
    /* Nonblocking ALSA writes plus a bounded retry delay let stop join without
     * waiting for a blocked pcm_writei or an indefinitely suspended device. */
    const struct timespec pause = {.tv_nsec = 1000000};
    (void)nanosleep(&pause, NULL);
}

static bool recover_output(CcAudioOutput *output, int error) {
    if (!atomic_load(&output->running))
        return false;
    int status;
    if (error == -ESTRPIPE) {
        /* snd_pcm_recover may wait for resume on suspend. Prepare restarts
         * instead, keeping the current block's unwritten samples. */
        status = output->pcm_prepare(output->pcm);
    } else if (error == -EPIPE || error == -EINTR) {
        status = output->pcm_recover(output->pcm, error, 1);
    } else {
        status = error;
    }
    if (status < 0)
        fail_output(output);
    return status >= 0 && atomic_load(&output->running);
}

static void *output_thread(void *context) {
    CcAudioOutput *output = context;
    float buffer[CC_AUDIO_OUTPUT_FRAMES * CC_AUDIO_OUTPUT_CHANNELS];
    while (atomic_load(&output->running)) {
        if (!render_buffer(output, buffer))
            break;
        unsigned long position = 0;
        while (position < CC_AUDIO_OUTPUT_FRAMES && atomic_load(&output->running)) {
            unsigned long remaining = CC_AUDIO_OUTPUT_FRAMES - position;
            long written = output->pcm_writei(
                output->pcm, buffer + position * CC_AUDIO_OUTPUT_CHANNELS, remaining);
            if (written == 0 || written == -EAGAIN) {
                wait_writable();
                continue;
            }
            if (written < 0) {
                if (!recover_output(output, (int)written))
                    break;
                continue;
            }
            if ((unsigned long)written > remaining) {
                fail_output(output);
                break;
            }
            position += (unsigned long)written;
        }
    }
    return NULL;
}

static bool load_alsa(CcAudioOutput *output) {
    output->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    return output->library &&
           load_symbol(output->library, "snd_pcm_open", &output->pcm_open,
                       sizeof(output->pcm_open)) &&
           load_symbol(output->library, "snd_pcm_close", &output->pcm_close,
                       sizeof(output->pcm_close)) &&
           load_symbol(output->library, "snd_pcm_drop", &output->pcm_drop,
                       sizeof(output->pcm_drop)) &&
           load_symbol(output->library, "snd_pcm_prepare", &output->pcm_prepare,
                       sizeof(output->pcm_prepare)) &&
           load_symbol(output->library, "snd_pcm_set_params", &output->pcm_set_params,
                       sizeof(output->pcm_set_params)) &&
           load_symbol(output->library, "snd_pcm_writei", &output->pcm_writei,
                       sizeof(output->pcm_writei)) &&
           load_symbol(output->library, "snd_pcm_recover", &output->pcm_recover,
                       sizeof(output->pcm_recover)) &&
           load_symbol(output->library, "snd_pcm_format_value",
                       &output->pcm_format_value, sizeof(output->pcm_format_value));
}

CcAudioOutput *cc_audio_output_platform_open(const CcAudioOutputOptions *options) {
    CcAudioOutput *output = calloc(1, sizeof(*output));
    if (!output)
        return NULL;
    output->options = *options;
    atomic_init(&output->running, false);
    atomic_init(&output->failed, false);
    atomic_init(&output->callbacks, 0);
    uint16_t endian = 1;
    const char *format_name = *(const uint8_t *)&endian ? "FLOAT_LE" : "FLOAT_BE";
    bool okay = load_alsa(output);
    int format = okay ? output->pcm_format_value(format_name) : -1;
    if (!okay || format < 0 ||
        output->pcm_open(&output->pcm, "default", 0, CC_ALSA_OPEN_NONBLOCK) < 0 ||
        output->pcm_set_params(output->pcm, format, CC_ALSA_ACCESS_RW_INTERLEAVED,
                               CC_AUDIO_OUTPUT_CHANNELS, options->sample_rate, 1,
                               CC_ALSA_BUFFER_MICROSECONDS) < 0) {
        cc_audio_output_close(output);
        return NULL;
    }
    return output;
}

bool cc_audio_output_start(CcAudioOutput *output) {
    if (!output || !output->pcm || atomic_load(&output->failed))
        return false;
    if (output->thread_started)
        return atomic_load(&output->running);
    if (output->pcm_prepare(output->pcm) < 0) {
        fail_output(output);
        return false;
    }
    atomic_store(&output->running, true);
    if (pthread_create(&output->thread, NULL, output_thread, output) != 0) {
        fail_output(output);
        return false;
    }
    output->thread_started = true;
    return true;
}

bool cc_audio_output_stop(CcAudioOutput *output) {
    if (!output)
        return true;
    atomic_store(&output->running, false);
    bool okay = true;
    if (output->thread_started) {
        okay = pthread_join(output->thread, NULL) == 0;
        if (okay) {
            output->thread_started = false;
            okay = output->pcm_drop(output->pcm) >= 0;
        }
        if (!okay)
            atomic_store(&output->failed, true);
    }
    while (atomic_load(&output->callbacks))
        sched_yield();
    return okay;
}

bool cc_audio_output_failed(const CcAudioOutput *output) {
    return output && atomic_load(&output->failed);
}

void cc_audio_output_close(CcAudioOutput *output) {
    if (!output)
        return;
    (void)cc_audio_output_stop(output);
    /* A failed join leaves the worker's output pointer borrowed. Rendering is
     * gated off, but retain its owner and function library until it retires. */
    if (output->thread_started)
        return;
    if (output->pcm && output->pcm_close)
        (void)output->pcm_close(output->pcm);
    if (output->library)
        (void)dlclose(output->library);
    free(output);
}
