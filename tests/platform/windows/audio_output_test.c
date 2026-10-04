#include <windows.h>
#include <mmreg.h>
#include <mmsystem.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { TEST_BUFFERS = 6 };

typedef struct {
    WAVEFORMATEX format;
    WAVEHDR *headers[TEST_BUFFERS];
    HANDLE event;
    HANDLE render_entered;
    HANDLE render_release;
    unsigned prepared;
    unsigned unprepared;
    unsigned closes;
    atomic_uint writes;
    atomic_uint renders;
    atomic_uint resets;
    atomic_bool rendering;
    bool fail_open;
    unsigned fail_prepare;
    bool fail_pause;
    bool fail_write;
    bool fail_restart;
    bool fail_reset;
    bool fail_close;
} OutputFixture;

static OutputFixture fixture;
static const uint32_t sample_words[] = {0x80000000u, 0x3eaaaaabu, 0xbf000001u,
                                        0x3f7fffffu, 0x40000000u};

static HWAVEOUT test_device(void) {
    return (HWAVEOUT)&fixture;
}

static void reset_fixture(void) {
    memset(&fixture, 0, sizeof(fixture));
    atomic_init(&fixture.writes, 0);
    atomic_init(&fixture.renders, 0);
    atomic_init(&fixture.resets, 0);
    atomic_init(&fixture.rendering, false);
}

static MMRESULT WINAPI test_open(LPHWAVEOUT device, UINT id, LPCWAVEFORMATEX format,
                                 DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
    assert(id == WAVE_MAPPER && !instance && flags == CALLBACK_EVENT);
    fixture.format = *format;
    fixture.event = (HANDLE)callback;
    if (fixture.fail_open)
        return MMSYSERR_ERROR;
    *device = test_device();
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_prepare(HWAVEOUT device, LPWAVEHDR header, UINT size) {
    assert(device == test_device() && size == sizeof(*header));
    if (fixture.fail_prepare == fixture.prepared + 1)
        return MMSYSERR_ERROR;
    assert(fixture.prepared < TEST_BUFFERS);
    fixture.headers[fixture.prepared++] = header;
    header->dwFlags = WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_write(HWAVEOUT device, LPWAVEHDR header, UINT size) {
    assert(device == test_device() && size == sizeof(*header));
    assert(header->dwBufferLength == 512 * 2 * sizeof(float));
    /* Negative zero, fractional samples, and values above unity must reach
     * the native API bit for bit, without clipping or adapter-side gain. */
    for (size_t index = 0; index < 512 * 2; ++index) {
        uint32_t word;
        memcpy(&word, header->lpData + index * sizeof(float), sizeof(word));
        assert(word == sample_words[index % 5]);
    }
    atomic_fetch_add(&fixture.writes, 1);
    if (fixture.fail_write)
        return MMSYSERR_ERROR;
    header->dwFlags = WHDR_PREPARED | WHDR_INQUEUE;
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_pause(HWAVEOUT device) {
    assert(device == test_device());
    return fixture.fail_pause ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_restart(HWAVEOUT device) {
    assert(device == test_device());
    return fixture.fail_restart ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_reset(HWAVEOUT device) {
    assert(device == test_device());
    assert(!atomic_load(&fixture.rendering));
    atomic_fetch_add(&fixture.resets, 1);
    if (fixture.fail_reset)
        return MMSYSERR_ERROR;
    for (unsigned index = 0; index < fixture.prepared; ++index)
        fixture.headers[index]->dwFlags =
            (fixture.headers[index]->dwFlags & WHDR_PREPARED) | WHDR_DONE;
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_unprepare(HWAVEOUT device, LPWAVEHDR header, UINT size) {
    assert(device == test_device() && size == sizeof(*header));
    assert(!(header->dwFlags & WHDR_INQUEUE));
    ++fixture.unprepared;
    header->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI test_close(HWAVEOUT device) {
    assert(device == test_device());
    ++fixture.closes;
    return fixture.fail_close ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}

#define waveOutOpen test_open
#define waveOutPrepareHeader test_prepare
#define waveOutWrite test_write
#define waveOutPause test_pause
#define waveOutRestart test_restart
#define waveOutReset test_reset
#define waveOutUnprepareHeader test_unprepare
#define waveOutClose test_close
#include "platform/windows/audio_output_windows.c"

static void render(void *context, float *samples, size_t frames) {
    assert(context == &fixture && frames == 512);
    atomic_store(&fixture.rendering, true);
    if (fixture.render_entered) {
        SetEvent(fixture.render_entered);
        assert(WaitForSingleObject(fixture.render_release, 2000) == WAIT_OBJECT_0);
    }
    for (size_t index = 0; index < frames * 2; ++index)
        memcpy(samples + index, &sample_words[index % 5], sizeof(float));
    atomic_fetch_add(&fixture.renders, 1);
    atomic_store(&fixture.rendering, false);
}

static CcAudioOutput *open_output(CcAudioOutputMode mode, unsigned rate) {
    CcAudioOutputOptions options = {
        .sample_rate = rate, .render = render, .context = &fixture, .mode = mode};
    return cc_audio_output_platform_open(&options);
}

static void test_lifetime(CcAudioOutputMode mode, unsigned rate) {
    reset_fixture();
    CcAudioOutput *output = open_output(mode, rate);
    assert(output && fixture.prepared == TEST_BUFFERS);
    assert(fixture.format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    assert(fixture.format.nSamplesPerSec == rate);
    assert(fixture.format.nChannels == 2 && fixture.format.wBitsPerSample == 32);
    assert(fixture.format.nBlockAlign == 8 &&
           fixture.format.nAvgBytesPerSec == rate * 8);
    assert(fixture.format.cbSize == 0);
    assert(cc_audio_output_start(output));
    assert(cc_audio_output_start(output));
    assert(atomic_load(&fixture.renders) == TEST_BUFFERS);
    assert(atomic_load(&fixture.writes) == TEST_BUFFERS);
    assert(cc_audio_output_stop(output));
    unsigned calls = atomic_load(&fixture.renders);
    SetEvent(fixture.event);
    Sleep(10);
    assert(atomic_load(&fixture.renders) == calls);
    assert(cc_audio_output_start(output));
    assert(output->next_buffer == 0);
    assert(atomic_load(&fixture.renders) == calls + TEST_BUFFERS);
    cc_audio_output_close(output);
    assert(fixture.unprepared == TEST_BUFFERS && fixture.closes == 1);
}

static unsigned __stdcall stop_output(void *context) {
    assert(cc_audio_output_stop(context));
    return 0;
}

static void test_stop_joins_renderer(void) {
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_DIRECT, 48000);
    assert(output && cc_audio_output_start(output));
    fixture.render_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    fixture.render_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(fixture.render_entered && fixture.render_release);
    fixture.headers[0]->dwFlags |= WHDR_DONE;
    SetEvent(fixture.event);
    assert(WaitForSingleObject(fixture.render_entered, 2000) == WAIT_OBJECT_0);
    HANDLE stopper = (HANDLE)_beginthreadex(NULL, 0, stop_output, output, 0, NULL);
    assert(stopper);
    assert(WaitForSingleObject(stopper, 20) == WAIT_TIMEOUT);
    assert(atomic_load(&fixture.resets) == 0);
    SetEvent(fixture.render_release);
    assert(WaitForSingleObject(stopper, 2000) == WAIT_OBJECT_0);
    assert(atomic_load(&fixture.resets) == 1);
    assert(atomic_load(&fixture.writes) == TEST_BUFFERS);
    CloseHandle(stopper);
    CloseHandle(fixture.render_entered);
    CloseHandle(fixture.render_release);
    cc_audio_output_close(output);
}

static void test_failures(void) {
    reset_fixture();
    fixture.fail_open = true;
    assert(!open_output(CC_AUDIO_OUTPUT_BUFFERED, 48000));
    assert(!fixture.prepared && !fixture.closes);
    for (unsigned index = 1; index <= TEST_BUFFERS; ++index) {
        reset_fixture();
        fixture.fail_prepare = index;
        assert(!open_output(CC_AUDIO_OUTPUT_BUFFERED, 48000));
        assert(fixture.unprepared == index - 1 && fixture.closes == 1);
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset_fixture();
        CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_BUFFERED, 48000);
        assert(output);
        fixture.fail_pause = failure == 0;
        fixture.fail_write = failure == 1;
        fixture.fail_restart = failure == 2;
        assert(!cc_audio_output_start(output));
        assert(cc_audio_output_failed(output));
        assert(!output->worker && !atomic_load(&output->running));
        assert(!cc_audio_output_start(output));
        cc_audio_output_close(output);
        assert(fixture.unprepared == TEST_BUFFERS && fixture.closes == 1);
    }
    reset_fixture();
    CcAudioOutput *output = open_output(CC_AUDIO_OUTPUT_BUFFERED, 48000);
    assert(output && cc_audio_output_start(output));
    fixture.fail_reset = true;
    assert(!cc_audio_output_stop(output) && cc_audio_output_failed(output));
    fixture.fail_reset = false;
    cc_audio_output_close(output);
    reset_fixture();
    output = open_output(CC_AUDIO_OUTPUT_BUFFERED, 48000);
    assert(output);
    fixture.fail_close = true;
    cc_audio_output_close(output);
    /* Driver close failure preserves storage and event ownership. */
    assert(output->event && fixture.closes == 1);
    fixture.fail_close = false;
    cc_audio_output_close(output);
    assert(fixture.closes == 2);
}

int main(void) {
    test_lifetime(CC_AUDIO_OUTPUT_BUFFERED, 32000);
    test_lifetime(CC_AUDIO_OUTPUT_BUFFERED, 48000);
    test_lifetime(CC_AUDIO_OUTPUT_DIRECT, 48000);
    test_stop_joins_renderer();
    test_failures();
    return 0;
}
