#include "audio/output_internal.h"

#include <AudioToolbox/AudioToolbox.h>

#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct CcAudioOutput {
    CcAudioOutputOptions options;
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[CC_AUDIO_OUTPUT_BUFFERS];
    AudioComponentInstance unit;
    atomic_bool enabled;
    atomic_bool failed;
    atomic_uint callbacks;
    bool running;
};

static bool output_enabled(const CcAudioOutput *output) {
    return atomic_load(&output->enabled) && !atomic_load(&output->failed);
}

static bool begin_callback(CcAudioOutput *output) {
    /* Count before testing the gate. The sequential atomic order makes stop's
     * closed gate visible to callbacks entering after its zero-count check. */
    atomic_fetch_add(&output->callbacks, 1);
    if (output_enabled(output))
        return true;
    atomic_fetch_sub(&output->callbacks, 1);
    return false;
}

static void end_callback(CcAudioOutput *output) {
    atomic_fetch_sub(&output->callbacks, 1);
}

static void wait_callbacks(CcAudioOutput *output) {
    while (atomic_load(&output->callbacks))
        sched_yield();
}

static bool refill_buffer(CcAudioOutput *output, AudioQueueRef queue,
                          AudioQueueBufferRef buffer) {
    if (!begin_callback(output))
        return false;
    output->options.render(output->options.context, buffer->mAudioData,
                           CC_AUDIO_OUTPUT_FRAMES);
    bool okay = output_enabled(output);
    if (okay) {
        buffer->mAudioDataByteSize =
            CC_AUDIO_OUTPUT_FRAMES * CC_AUDIO_OUTPUT_CHANNELS * sizeof(float);
        okay = AudioQueueEnqueueBuffer(queue, buffer, 0, NULL) == noErr;
        if (!okay)
            atomic_store(&output->failed, true);
    }
    end_callback(output);
    return okay;
}

static void queue_callback(void *context, AudioQueueRef queue,
                           AudioQueueBufferRef buffer) {
    (void)refill_buffer(context, queue, buffer);
}

static void silence_buffers(AudioBufferList *buffers) {
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        if (buffers->mBuffers[index].mData)
            memset(buffers->mBuffers[index].mData, 0,
                   buffers->mBuffers[index].mDataByteSize);
    }
}

static OSStatus unit_callback(void *context, AudioUnitRenderActionFlags *flags,
                              const AudioTimeStamp *time, UInt32 bus, UInt32 frames,
                              AudioBufferList *buffers) {
    (void)flags;
    (void)time;
    (void)bus;
    CcAudioOutput *output = context;
    if (!buffers)
        return noErr;
    if (buffers->mNumberBuffers != 1 ||
        buffers->mBuffers[0].mNumberChannels != CC_AUDIO_OUTPUT_CHANNELS ||
        !buffers->mBuffers[0].mData ||
        buffers->mBuffers[0].mDataByteSize <
            (uint64_t)frames * CC_AUDIO_OUTPUT_CHANNELS * sizeof(float)) {
        silence_buffers(buffers);
        return noErr;
    }
    if (!begin_callback(output)) {
        silence_buffers(buffers);
        return noErr;
    }
    output->options.render(output->options.context, buffers->mBuffers[0].mData, frames);
    end_callback(output);
    return noErr;
}

static AudioStreamBasicDescription output_format(unsigned sample_rate) {
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mBytesPerPacket = CC_AUDIO_OUTPUT_CHANNELS * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = CC_AUDIO_OUTPUT_CHANNELS * sizeof(float);
    format.mChannelsPerFrame = CC_AUDIO_OUTPUT_CHANNELS;
    format.mBitsPerChannel = 8u * sizeof(float);
    return format;
}

static bool open_queue(CcAudioOutput *output,
                       const AudioStreamBasicDescription *format) {
    if (AudioQueueNewOutput(format, queue_callback, output, NULL, NULL, 0,
                            &output->queue) != noErr)
        return false;
    for (unsigned index = 0; index < CC_AUDIO_OUTPUT_BUFFERS; ++index) {
        if (AudioQueueAllocateBuffer(output->queue,
                                     CC_AUDIO_OUTPUT_FRAMES * CC_AUDIO_OUTPUT_CHANNELS *
                                         sizeof(float),
                                     &output->buffers[index]) != noErr)
            return false;
    }
    return true;
}

static bool open_unit(CcAudioOutput *output,
                      const AudioStreamBasicDescription *format) {
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (!component || AudioComponentInstanceNew(component, &output->unit) != noErr)
        return false;
    AURenderCallbackStruct callback = {.inputProc = unit_callback,
                                       .inputProcRefCon = output};
    return AudioUnitSetProperty(output->unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, format,
                                sizeof(*format)) == noErr &&
           AudioUnitSetProperty(output->unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &callback,
                                sizeof(callback)) == noErr &&
           AudioUnitInitialize(output->unit) == noErr;
}

CcAudioOutput *cc_audio_output_platform_open(const CcAudioOutputOptions *options) {
    CcAudioOutput *output = calloc(1, sizeof(*output));
    if (!output)
        return NULL;
    output->options = *options;
    atomic_init(&output->enabled, false);
    atomic_init(&output->failed, false);
    atomic_init(&output->callbacks, 0);
    AudioStreamBasicDescription format = output_format(options->sample_rate);
    bool okay = options->mode == CC_AUDIO_OUTPUT_BUFFERED ? open_queue(output, &format)
                                                          : open_unit(output, &format);
    if (!okay) {
        cc_audio_output_close(output);
        return NULL;
    }
    return output;
}

bool cc_audio_output_start(CcAudioOutput *output) {
    if (!output || atomic_load(&output->failed))
        return false;
    if (output->running)
        return true;
    atomic_store(&output->enabled, true);
    output->running = true;
    bool okay = true;
    if (output->queue) {
        for (unsigned index = 0; index < CC_AUDIO_OUTPUT_BUFFERS && okay; ++index)
            okay = refill_buffer(output, output->queue, output->buffers[index]);
        if (okay)
            okay = AudioQueueStart(output->queue, NULL) == noErr;
    } else {
        okay = AudioOutputUnitStart(output->unit) == noErr;
    }
    if (!okay || !output_enabled(output)) {
        atomic_store(&output->failed, true);
        (void)cc_audio_output_stop(output);
        return false;
    }
    return true;
}

bool cc_audio_output_stop(CcAudioOutput *output) {
    if (!output)
        return true;
    atomic_store(&output->enabled, false);
    bool okay = true;
    if (output->running) {
        /* Queue stop resets pending buffers and may invoke callbacks now. */
        okay = output->queue ? AudioQueueStop(output->queue, true) == noErr
                             : AudioOutputUnitStop(output->unit) == noErr;
        output->running = false;
        if (!okay)
            atomic_store(&output->failed, true);
    }
    wait_callbacks(output);
    return okay;
}

bool cc_audio_output_failed(const CcAudioOutput *output) {
    return output && atomic_load(&output->failed);
}

void cc_audio_output_close(CcAudioOutput *output) {
    if (!output)
        return;
    (void)cc_audio_output_stop(output);
    OSStatus status = noErr;
    if (output->queue)
        status = AudioQueueDispose(output->queue, true);
    if (output->unit) {
        (void)AudioUnitUninitialize(output->unit);
        status = AudioComponentInstanceDispose(output->unit);
    }
    /* A failed disposal cannot establish callback-userdata lifetime. Keep the
     * disabled owner alive rather than freeing a still-borrowed callback ref. */
    if (status == noErr)
        free(output);
}
