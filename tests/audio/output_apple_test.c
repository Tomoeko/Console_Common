#include "console_common/audio/output.h"

#include <AudioToolbox/AudioToolbox.h>

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public Apple types and first-party fault injection exercise the real host
 * module without opening hardware or changing any synthesis behavior. */
static float queue_samples[3][1024];
static AudioQueueBuffer queue_buffers[3] = {
    {.mAudioDataBytesCapacity = 4096, .mAudioData = queue_samples[0]},
    {.mAudioDataBytesCapacity = 4096, .mAudioData = queue_samples[1]},
    {.mAudioDataBytesCapacity = 4096, .mAudioData = queue_samples[2]},
};

typedef struct {
    AudioQueueOutputCallback queue_callback;
    AURenderCallbackStruct unit_callback;
    void *queue_context;
    unsigned queue_identity;
    unsigned unit_identity;
    unsigned component_identity;
    unsigned sample_rate;
    unsigned allocations;
    unsigned buffers;
    unsigned enqueues;
    unsigned starts;
    unsigned stops;
    unsigned disposals;
    unsigned uninitializes;
    unsigned rendered;
    unsigned fail_buffer;
    unsigned fail_enqueue;
    unsigned fail_unit_step;
    bool fail_owner;
    bool fail_new;
    bool fail_start;
    bool fail_stop;
    bool fail_dispose;
    bool callback_on_start;
} OutputFixture;

static OutputFixture fixture;
static atomic_bool block_renderer;
static atomic_bool renderer_entered;
static atomic_bool renderer_release;
static atomic_bool stop_returned;

static AudioQueueRef fixture_queue(void) {
    return (AudioQueueRef)&fixture.queue_identity;
}

static AudioComponentInstance fixture_unit(void) {
    return (AudioComponentInstance)&fixture.unit_identity;
}

static void *output_allocate(size_t count, size_t size) {
    return fixture.fail_owner ? NULL : calloc(count, size);
}

static void render_samples(void *context, float *stereo, size_t frames) {
    assert(context == &fixture && stereo);
    if (atomic_load(&block_renderer)) {
        atomic_store(&renderer_entered, true);
        while (!atomic_load(&renderer_release))
            sched_yield();
    }
    fixture.rendered += (unsigned)frames;
    const uint32_t words[] = {0x80000000u, 0x3eaaaaabu, 0xbf000001u, 0x3f7fffffu};
    for (size_t index = 0; index < frames * 2; ++index)
        memcpy(stereo + index, &words[index % 4], sizeof(float));
}

static void check_sample_words(const float *stereo, size_t frames) {
    const uint32_t words[] = {0x80000000u, 0x3eaaaaabu, 0xbf000001u, 0x3f7fffffu};
    for (size_t index = 0; index < frames * 2; ++index) {
        uint32_t word;
        memcpy(&word, stereo + index, sizeof(word));
        assert(word == words[index % 4]);
    }
}

static OSStatus queue_new(const AudioStreamBasicDescription *format,
                          AudioQueueOutputCallback callback, void *context,
                          CFRunLoopRef loop, CFStringRef mode, UInt32 flags,
                          AudioQueueRef *output) {
    assert(format->mSampleRate == fixture.sample_rate && !loop && !mode && !flags);
    assert(format->mChannelsPerFrame == 2 && format->mBytesPerFrame == 8);
    assert(format->mFormatID == kAudioFormatLinearPCM);
    assert(format->mFormatFlags == kAudioFormatFlagsNativeFloatPacked);
    if (fixture.fail_new)
        return -1;
    fixture.queue_callback = callback;
    fixture.queue_context = context;
    *output = fixture_queue();
    return noErr;
}

static OSStatus queue_allocate(AudioQueueRef queue, UInt32 bytes,
                               AudioQueueBufferRef *output) {
    assert(queue == fixture_queue() && bytes == 4096);
    ++fixture.allocations;
    if (fixture.allocations == fixture.fail_buffer)
        return -1;
    assert(fixture.buffers < 3);
    *output = &queue_buffers[fixture.buffers++];
    return noErr;
}

static OSStatus queue_enqueue(AudioQueueRef queue, AudioQueueBufferRef buffer,
                              UInt32 packets,
                              const AudioStreamPacketDescription *descriptions) {
    assert(queue == fixture_queue() && buffer && !packets && !descriptions);
    assert(buffer->mAudioDataByteSize == 4096);
    check_sample_words(buffer->mAudioData, 512);
    ++fixture.enqueues;
    return fixture.enqueues == fixture.fail_enqueue ? -1 : noErr;
}

static OSStatus queue_start(AudioQueueRef queue, const AudioTimeStamp *timestamp) {
    assert(queue == fixture_queue() && !timestamp);
    ++fixture.starts;
    if (fixture.callback_on_start)
        fixture.queue_callback(fixture.queue_context, queue, &queue_buffers[0]);
    return fixture.fail_start ? -1 : noErr;
}

static void pending_queue_callbacks(AudioQueueRef queue) {
    for (unsigned index = 0; index < fixture.buffers; ++index)
        fixture.queue_callback(fixture.queue_context, queue, &queue_buffers[index]);
}

static OSStatus queue_stop(AudioQueueRef queue, Boolean immediate) {
    assert(queue == fixture_queue() && immediate);
    ++fixture.stops;
    pending_queue_callbacks(queue);
    return fixture.fail_stop ? -1 : noErr;
}

static OSStatus queue_dispose(AudioQueueRef queue, Boolean immediate) {
    assert(queue == fixture_queue() && immediate);
    ++fixture.disposals;
    pending_queue_callbacks(queue);
    return fixture.fail_dispose ? -1 : noErr;
}

static AudioComponent unit_find(AudioComponent previous,
                                const AudioComponentDescription *description) {
    assert(!previous && description->componentType == kAudioUnitType_Output);
    assert(description->componentSubType == kAudioUnitSubType_DefaultOutput);
    assert(description->componentManufacturer == kAudioUnitManufacturer_Apple);
    return fixture.fail_unit_step == 1 ? NULL
                                       : (AudioComponent)&fixture.component_identity;
}

static OSStatus unit_new(AudioComponent component, AudioComponentInstance *output) {
    assert(component == (AudioComponent)&fixture.component_identity);
    if (fixture.fail_unit_step == 2)
        return -1;
    *output = fixture_unit();
    return noErr;
}

static OSStatus unit_property(AudioUnit unit, AudioUnitPropertyID property,
                              AudioUnitScope scope, AudioUnitElement element,
                              const void *value, UInt32 size) {
    assert(unit == fixture_unit() && scope == kAudioUnitScope_Input && !element);
    if (property == kAudioUnitProperty_StreamFormat) {
        assert(size == sizeof(AudioStreamBasicDescription));
        const AudioStreamBasicDescription *format = value;
        assert(format->mSampleRate == fixture.sample_rate);
        assert(format->mChannelsPerFrame == 2 && format->mBytesPerFrame == 8);
        assert(format->mFormatFlags == kAudioFormatFlagsNativeFloatPacked);
        return fixture.fail_unit_step == 3 ? -1 : noErr;
    }
    assert(property == kAudioUnitProperty_SetRenderCallback);
    assert(size == sizeof(AURenderCallbackStruct));
    if (fixture.fail_unit_step == 4)
        return -1;
    fixture.unit_callback = *(const AURenderCallbackStruct *)value;
    return noErr;
}

static OSStatus unit_initialize(AudioUnit unit) {
    assert(unit == fixture_unit());
    return fixture.fail_unit_step == 5 ? -1 : noErr;
}

static OSStatus invoke_unit_callback(UInt32 frames, AudioBufferList *buffers) {
    AudioUnitRenderActionFlags flags = 0;
    AudioTimeStamp time = {0};
    return fixture.unit_callback.inputProc(fixture.unit_callback.inputProcRefCon,
                                           &flags, &time, 0, frames, buffers);
}

static void pending_unit_callback(void) {
    if (!fixture.unit_callback.inputProc)
        return;
    float samples[64];
    for (size_t index = 0; index < 64; ++index)
        samples[index] = 1.0f;
    AudioBufferList buffers = {.mNumberBuffers = 1,
                               .mBuffers = {{.mNumberChannels = 2,
                                             .mDataByteSize = sizeof(samples),
                                             .mData = samples}}};
    assert(invoke_unit_callback(32, &buffers) == noErr);
}

static OSStatus unit_start(AudioUnit unit) {
    assert(unit == fixture_unit());
    ++fixture.starts;
    if (fixture.callback_on_start)
        pending_unit_callback();
    return fixture.fail_start ? -1 : noErr;
}

static OSStatus unit_stop(AudioUnit unit) {
    assert(unit == fixture_unit());
    ++fixture.stops;
    unsigned rendered = fixture.rendered;
    pending_unit_callback();
    assert(fixture.rendered == rendered);
    return fixture.fail_stop ? -1 : noErr;
}

static OSStatus unit_uninitialize(AudioUnit unit) {
    assert(unit == fixture_unit());
    ++fixture.uninitializes;
    return noErr;
}

static OSStatus unit_dispose(AudioComponentInstance unit) {
    assert(unit == fixture_unit());
    ++fixture.disposals;
    unsigned rendered = fixture.rendered;
    pending_unit_callback();
    assert(fixture.rendered == rendered);
    return fixture.fail_dispose ? -1 : noErr;
}

#define calloc output_allocate
#define AudioQueueNewOutput queue_new
#define AudioQueueAllocateBuffer queue_allocate
#define AudioQueueEnqueueBuffer queue_enqueue
#define AudioQueueStart queue_start
#define AudioQueueStop queue_stop
#define AudioQueueDispose queue_dispose
#define AudioComponentFindNext unit_find
#define AudioComponentInstanceNew unit_new
#define AudioUnitSetProperty unit_property
#define AudioUnitInitialize unit_initialize
#define AudioOutputUnitStart unit_start
#define AudioOutputUnitStop unit_stop
#define AudioUnitUninitialize unit_uninitialize
#define AudioComponentInstanceDispose unit_dispose
#include "../../src/audio/output.c"
#include "../../src/platform/apple/audio_output_apple.c"
#undef calloc

static CcAudioOutputOptions output_options(CcAudioOutputMode mode) {
    return (CcAudioOutputOptions){.sample_rate = fixture.sample_rate,
                                  .render = render_samples,
                                  .context = &fixture,
                                  .mode = mode};
}

static void reset_fixture(void) {
    fixture = (OutputFixture){.sample_rate = 48000};
    atomic_store(&block_renderer, false);
    atomic_store(&renderer_entered, false);
    atomic_store(&renderer_release, false);
    atomic_store(&stop_returned, false);
}

static void test_invalid_options(void) {
    reset_fixture();
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    assert(!cc_audio_output_open(NULL));
    options.sample_rate = 7999;
    assert(!cc_audio_output_open(&options));
    options.sample_rate = 192001;
    assert(!cc_audio_output_open(&options));
    options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    options.render = NULL;
    assert(!cc_audio_output_open(&options));
    options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    options.mode = (CcAudioOutputMode)2;
    assert(!cc_audio_output_open(&options));
    assert(!cc_audio_output_start(NULL));
    assert(cc_audio_output_stop(NULL));
    assert(!cc_audio_output_failed(NULL));
    cc_audio_output_close(NULL);
}

static void test_queue_setup_failures(void) {
    for (unsigned failure = 0; failure < 9; ++failure) {
        reset_fixture();
        if (failure == 0)
            fixture.fail_owner = true;
        else if (failure == 1)
            fixture.fail_new = true;
        else if (failure < 5)
            fixture.fail_buffer = failure - 1;
        else if (failure < 8)
            fixture.fail_enqueue = failure - 4;
        else
            fixture.fail_start = true;
        CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
        CcAudioOutput *output = cc_audio_output_open(&options);
        assert(fixture.rendered == 0 && !fixture.enqueues && !fixture.starts);
        if (failure < 5) {
            assert(!output);
        } else {
            assert(output && !cc_audio_output_start(output));
            assert(cc_audio_output_failed(output));
            unsigned expected = failure < 8 ? (failure - 4) * 512 : 1536;
            assert(fixture.rendered == expected);
            assert(fixture.starts == (failure == 8 ? 1u : 0u));
            assert(!cc_audio_output_start(output));
            cc_audio_output_close(output);
            assert(fixture.rendered == expected);
        }
        assert(fixture.disposals == (failure > 1 ? 1u : 0u));
    }
}

static void test_queue_callbacks(bool fail) {
    reset_fixture();
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output && fixture.rendered == 0);
    assert(cc_audio_output_start(output));
    assert(fixture.rendered == 1536 && fixture.enqueues == 3 && fixture.starts == 1);
    assert(cc_audio_output_start(output));
    assert(fixture.allocations == 3 && fixture.rendered == 1536);
    if (fail)
        fixture.fail_enqueue = 4;
    fixture.queue_callback(fixture.queue_context, fixture_queue(), &queue_buffers[0]);
    assert(fixture.rendered == 2048 && fixture.enqueues == 4);
    assert(cc_audio_output_start(output) == !fail);
    assert(cc_audio_output_failed(output) == fail);
    if (fail) {
        pending_queue_callbacks(fixture_queue());
        assert(fixture.rendered == 2048 && fixture.enqueues == 4);
    }
    assert(cc_audio_output_stop(output));
    assert(fixture.stops == 1 && fixture.rendered == 2048);
    assert(cc_audio_output_stop(output) && fixture.stops == 1);
    cc_audio_output_close(output);
    assert(fixture.disposals == 1 && fixture.rendered == 2048);
}

static void test_queue_start_callback_failure(void) {
    reset_fixture();
    fixture.fail_enqueue = 4;
    fixture.callback_on_start = true;
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output && !cc_audio_output_start(output));
    assert(fixture.rendered == 2048 && fixture.enqueues == 4);
    assert(fixture.starts == 1 && cc_audio_output_failed(output));
    cc_audio_output_close(output);
    assert(fixture.disposals == 1 && fixture.rendered == 2048);
}

static void test_queue_restart_and_rates(void) {
    const unsigned rates[] = {8000, 44100, 192000};
    for (size_t index = 0; index < sizeof(rates) / sizeof(rates[0]); ++index) {
        reset_fixture();
        fixture.sample_rate = rates[index];
        CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
        CcAudioOutput *output = cc_audio_output_open(&options);
        assert(output && cc_audio_output_start(output));
        assert(cc_audio_output_stop(output));
        assert(cc_audio_output_start(output));
        assert(fixture.rendered == 3072 && fixture.enqueues == 6);
        assert(fixture.allocations == 3 && fixture.starts == 2);
        cc_audio_output_close(output);
        assert(fixture.stops == 2 && fixture.disposals == 1);
    }
}

static void test_unit_setup_failures(void) {
    for (unsigned failure = 0; failure <= 6; ++failure) {
        reset_fixture();
        if (!failure)
            fixture.fail_owner = true;
        else if (failure <= 5)
            fixture.fail_unit_step = failure;
        else
            fixture.fail_start = true;
        CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_DIRECT);
        CcAudioOutput *output = cc_audio_output_open(&options);
        assert(!fixture.rendered && !fixture.starts);
        if (failure < 6) {
            assert(!output);
        } else {
            assert(output && !cc_audio_output_start(output));
            assert(cc_audio_output_failed(output));
            cc_audio_output_close(output);
        }
        assert(fixture.disposals == (failure >= 3 ? 1u : 0u));
        assert(fixture.uninitializes == fixture.disposals);
    }
}

static void test_unit_callback_sizes(void) {
    reset_fixture();
    fixture.sample_rate = 44100;
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_DIRECT);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output);
    pending_unit_callback();
    assert(!fixture.rendered);
    assert(cc_audio_output_start(output) && !fixture.rendered);
    assert(cc_audio_output_start(output) && fixture.starts == 1);
    const unsigned sizes[] = {1, 17, 511, 777};
    float samples[2048];
    unsigned total = 0;
    for (size_t index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        unsigned frames = sizes[index];
        AudioBufferList buffers = {
            .mNumberBuffers = 1,
            .mBuffers = {{.mNumberChannels = 2,
                          .mDataByteSize = frames * 2u * sizeof(float),
                          .mData = samples}}};
        assert(invoke_unit_callback(frames, &buffers) == noErr);
        check_sample_words(samples, frames);
        total += frames;
        assert(fixture.rendered == total);
    }
    assert(cc_audio_output_stop(output));
    assert(fixture.rendered == total && fixture.stops == 1);
    assert(cc_audio_output_start(output) && fixture.starts == 2);
    cc_audio_output_close(output);
    assert(fixture.rendered == total && fixture.stops == 2 && fixture.disposals == 1);
}

static void test_unit_malformed_buffers(void) {
    reset_fixture();
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_DIRECT);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output && cc_audio_output_start(output));
    float samples[2][16];
    AudioBufferList *buffers = malloc(sizeof(*buffers) + sizeof(AudioBuffer));
    assert(buffers);
    for (unsigned failure = 0; failure < 4; ++failure) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            for (size_t sample = 0; sample < 16; ++sample)
                samples[channel][sample] = 1.0f;
            buffers->mBuffers[channel] = (AudioBuffer){
                .mNumberChannels = 2, .mDataByteSize = 64, .mData = samples[channel]};
        }
        buffers->mNumberBuffers = failure == 0 ? 2 : 1;
        if (failure == 1)
            buffers->mBuffers[0].mNumberChannels = 1;
        if (failure == 2)
            buffers->mBuffers[0].mDataByteSize = 32;
        if (failure == 3)
            buffers->mBuffers[0].mData = NULL;
        assert(invoke_unit_callback(8, buffers) == noErr);
        assert(fixture.rendered == 0);
        if (failure < 3) {
            size_t words = buffers->mBuffers[0].mDataByteSize / sizeof(float);
            for (size_t sample = 0; sample < words; ++sample)
                assert(samples[0][sample] == 0.0f);
        }
        if (!failure) {
            for (size_t sample = 0; sample < 16; ++sample)
                assert(samples[1][sample] == 0.0f);
        }
    }
    assert(!cc_audio_output_failed(output));
    free(buffers);
    cc_audio_output_close(output);
}

static void test_stop_and_dispose_failures(CcAudioOutputMode mode) {
    reset_fixture();
    CcAudioOutputOptions options = output_options(mode);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output && cc_audio_output_start(output));
    unsigned rendered = fixture.rendered;
    fixture.fail_stop = true;
    assert(!cc_audio_output_stop(output));
    assert(cc_audio_output_failed(output) && !cc_audio_output_start(output));
    assert(fixture.rendered == rendered);
    fixture.fail_dispose = true;
    cc_audio_output_close(output);
    assert(fixture.rendered == rendered);
    /* Failed host disposal must retain the disabled callback owner. */
    fixture.fail_dispose = false;
    cc_audio_output_close(output);
    assert(fixture.disposals == 2 && fixture.rendered == rendered);
}

static void *callback_thread(void *context) {
    (void)context;
    fixture.queue_callback(fixture.queue_context, fixture_queue(), &queue_buffers[0]);
    return NULL;
}

static void *stop_thread(void *context) {
    assert(cc_audio_output_stop(context));
    atomic_store(&stop_returned, true);
    return NULL;
}

static void test_inflight_stop(void) {
    reset_fixture();
    CcAudioOutputOptions options = output_options(CC_AUDIO_OUTPUT_BUFFERED);
    CcAudioOutput *output = cc_audio_output_open(&options);
    assert(output && cc_audio_output_start(output));
    atomic_store(&block_renderer, true);
    pthread_t callback_worker, stop_worker;
    assert(pthread_create(&callback_worker, NULL, callback_thread, NULL) == 0);
    while (!atomic_load(&renderer_entered))
        sched_yield();
    assert(pthread_create(&stop_worker, NULL, stop_thread, output) == 0);
    while (atomic_load(&output->enabled))
        sched_yield();
    assert(!atomic_load(&stop_returned));
    atomic_store(&renderer_release, true);
    assert(pthread_join(callback_worker, NULL) == 0);
    assert(pthread_join(stop_worker, NULL) == 0);
    assert(atomic_load(&stop_returned));
    assert(fixture.rendered == 2048 && fixture.enqueues == 3);
    cc_audio_output_close(output);
}

int main(void) {
    test_invalid_options();
    test_queue_setup_failures();
    test_queue_callbacks(false);
    test_queue_callbacks(true);
    test_queue_start_callback_failure();
    test_queue_restart_and_rates();
    test_unit_setup_failures();
    test_unit_callback_sizes();
    test_unit_malformed_buffers();
    test_stop_and_dispose_failures(CC_AUDIO_OUTPUT_BUFFERED);
    test_stop_and_dispose_failures(CC_AUDIO_OUTPUT_DIRECT);
    test_inflight_stop();
    puts("Shared Apple audio output tests passed.");
    return 0;
}
