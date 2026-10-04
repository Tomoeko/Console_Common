#include "audio/output_internal.h"

#include <windows.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <avrt.h>
#include <process.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { CC_WINDOWS_AUDIO_BUFFERS = 6 };

struct CcAudioOutput {
    CcAudioOutputOptions options;
    HWAVEOUT device;
    HANDLE event;
    HANDLE worker;
    WAVEHDR headers[CC_WINDOWS_AUDIO_BUFFERS];
    float samples[CC_WINDOWS_AUDIO_BUFFERS][CC_AUDIO_OUTPUT_FRAMES * 2];
    unsigned next_buffer;
    atomic_bool running;
    atomic_bool failed;
};

static bool submit_buffer(CcAudioOutput *output, unsigned index) {
    if (!atomic_load(&output->running) || atomic_load(&output->failed))
        return false;
    output->options.render(output->options.context, output->samples[index],
                           CC_AUDIO_OUTPUT_FRAMES);
    if (!atomic_load(&output->running))
        return false;
    if (waveOutWrite(output->device, &output->headers[index], sizeof(WAVEHDR)) !=
        MMSYSERR_NOERROR) {
        atomic_store(&output->failed, true);
        atomic_store(&output->running, false);
        return false;
    }
    return true;
}

static bool refill_completed(CcAudioOutput *output) {
    for (unsigned count = 0; count < CC_WINDOWS_AUDIO_BUFFERS; ++count) {
        unsigned index = output->next_buffer;
        if (!(output->headers[index].dwFlags & WHDR_DONE))
            break;
        /* Completion events can coalesce. Requeue in playback order, including
         * across the ring boundary, so adjacent DSP blocks cannot be swapped. */
        if (!submit_buffer(output, index))
            return false;
        output->next_buffer = (index + 1) % CC_WINDOWS_AUDIO_BUFFERS;
    }
    return true;
}

static unsigned __stdcall output_worker(void *context) {
    CcAudioOutput *output = context;
    DWORD task_index = 0;
    HANDLE scheduling = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
    if (scheduling)
        AvSetMmThreadPriority(scheduling, AVRT_PRIORITY_HIGH);
    while (atomic_load(&output->running)) {
        DWORD status = WaitForSingleObject(output->event, 1000);
        if (status == WAIT_TIMEOUT)
            continue;
        if (status != WAIT_OBJECT_0) {
            atomic_store(&output->failed, true);
            atomic_store(&output->running, false);
            break;
        }
        if (!refill_completed(output))
            break;
    }
    if (scheduling)
        AvRevertMmThreadCharacteristics(scheduling);
    return 0;
}

CcAudioOutput *cc_audio_output_platform_open(const CcAudioOutputOptions *options) {
    CcAudioOutput *output = calloc(1, sizeof(*output));
    if (!output)
        return NULL;
    output->options = *options;
    atomic_init(&output->running, false);
    atomic_init(&output->failed, false);
    output->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!output->event)
        goto release_output;
    WAVEFORMATEX format = {.wFormatTag = WAVE_FORMAT_IEEE_FLOAT,
                           .nChannels = 2,
                           .nSamplesPerSec = options->sample_rate,
                           .nAvgBytesPerSec = options->sample_rate * 8,
                           .nBlockAlign = 8,
                           .wBitsPerSample = 32};
    /* Pass the caller's reconstructed stereo floats unchanged. Windows owns
     * device-rate conversion; no voice gains or DSP effects run in this adapter. */
    if (waveOutOpen(&output->device, WAVE_MAPPER, &format, (DWORD_PTR)output->event, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR)
        goto release_output;
    for (unsigned index = 0; index < CC_WINDOWS_AUDIO_BUFFERS; ++index) {
        output->headers[index].lpData = (LPSTR)output->samples[index];
        output->headers[index].dwBufferLength = sizeof(output->samples[index]);
        if (waveOutPrepareHeader(output->device, &output->headers[index],
                                 sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
            goto release_output;
    }
    return output;
release_output:
    cc_audio_output_close(output);
    return NULL;
}

bool cc_audio_output_start(CcAudioOutput *output) {
    if (!output || atomic_load(&output->failed))
        return false;
    if (output->worker)
        return atomic_load(&output->running);
    if (waveOutPause(output->device) != MMSYSERR_NOERROR)
        goto fail_start;
    atomic_store(&output->running, true);
    output->next_buffer = 0;
    for (unsigned index = 0; index < CC_WINDOWS_AUDIO_BUFFERS; ++index) {
        if (!submit_buffer(output, index))
            goto fail_start;
    }
    output->worker = (HANDLE)_beginthreadex(NULL, 0, output_worker, output, 0, NULL);
    if (!output->worker || waveOutRestart(output->device) != MMSYSERR_NOERROR)
        goto fail_start;
    return true;
fail_start:
    atomic_store(&output->failed, true);
    cc_audio_output_stop(output);
    return false;
}

bool cc_audio_output_stop(CcAudioOutput *output) {
    if (!output)
        return true;
    atomic_store(&output->running, false);
    if (output->worker) {
        SetEvent(output->event);
        WaitForSingleObject(output->worker, INFINITE);
        CloseHandle(output->worker);
        output->worker = NULL;
    }
    /* The worker is joined before reset: no borrowed renderer or buffer can
     * race a device reset, audio-state rewind, or owner destruction. */
    if (output->device && waveOutReset(output->device) != MMSYSERR_NOERROR)
        atomic_store(&output->failed, true);
    return !atomic_load(&output->failed);
}

bool cc_audio_output_failed(const CcAudioOutput *output) {
    return output && atomic_load(&output->failed);
}

void cc_audio_output_close(CcAudioOutput *output) {
    if (!output)
        return;
    cc_audio_output_stop(output);
    if (output->device) {
        for (unsigned index = 0; index < CC_WINDOWS_AUDIO_BUFFERS; ++index) {
            if (output->headers[index].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(output->device, &output->headers[index],
                                       sizeof(WAVEHDR));
        }
        if (waveOutClose(output->device) != MMSYSERR_NOERROR) {
            /* A failed driver close can still retain WAVEHDR pointers. Keep
             * their storage alive rather than freeing memory owned by it. */
            fprintf(stderr, "Windows: audio driver could not release its buffers.\n");
            return;
        }
    }
    if (output->event)
        CloseHandle(output->event);
    free(output);
}
