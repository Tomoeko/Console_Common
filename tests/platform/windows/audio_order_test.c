#include <windows.h>
#include <mmsystem.h>

#include <assert.h>

static unsigned submitted[16];
static unsigned submitted_count;
static WAVEHDR *first_header;
static MMRESULT WINAPI test_write(HWAVEOUT device, LPWAVEHDR header, UINT size) {
    (void)device;
    assert(size == sizeof(*header));
    assert(submitted_count < sizeof(submitted) / sizeof(submitted[0]));
    submitted[submitted_count++] = (unsigned)(header - first_header);
    header->dwFlags &= ~WHDR_DONE;
    return MMSYSERR_NOERROR;
}

#define waveOutWrite test_write
#include "platform/windows/audio_output_windows.c"
#undef waveOutWrite

static void render(void *context, float *samples, size_t frames) {
    unsigned *calls = context;
    ++*calls;
    assert(frames == CC_AUDIO_OUTPUT_FRAMES);
    samples[0] = (float)*calls;
}

int main(void) {
    unsigned calls = 0;
    CcAudioOutput output = {.options = {.render = render, .context = &calls}};
    atomic_init(&output.running, true);
    atomic_init(&output.failed, false);
    first_header = output.headers;
    output.next_buffer = CC_WINDOWS_AUDIO_BUFFERS - 2;
    for (unsigned index = 0; index < CC_WINDOWS_AUDIO_BUFFERS; ++index)
        output.headers[index].dwFlags = WHDR_DONE;
    assert(refill_completed(&output));
    assert(calls == CC_WINDOWS_AUDIO_BUFFERS);
    for (unsigned index = 0; index < CC_WINDOWS_AUDIO_BUFFERS; ++index)
        assert(submitted[index] ==
               (index + CC_WINDOWS_AUDIO_BUFFERS - 2) % CC_WINDOWS_AUDIO_BUFFERS);
    assert(refill_completed(&output));
    assert(calls == CC_WINDOWS_AUDIO_BUFFERS);
    /* A later completion must not jump over the next queued buffer. */
    output.headers[0].dwFlags = WHDR_DONE;
    assert(refill_completed(&output));
    assert(calls == CC_WINDOWS_AUDIO_BUFFERS);
    output.headers[CC_WINDOWS_AUDIO_BUFFERS - 2].dwFlags = WHDR_DONE;
    assert(refill_completed(&output));
    assert(calls == CC_WINDOWS_AUDIO_BUFFERS + 1);
    atomic_store(&output.running, false);
    output.headers[CC_WINDOWS_AUDIO_BUFFERS - 1].dwFlags = WHDR_DONE;
    assert(!refill_completed(&output));
    assert(calls == CC_WINDOWS_AUDIO_BUFFERS + 1);
    return 0;
}
