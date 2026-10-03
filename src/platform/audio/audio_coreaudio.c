#include "console_common/platform/audio.h"

#include "console_common/support/error.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AudioSlot {
    AudioQueueBufferRef buffer;
    atomic_bool queued;
} AudioSlot;

struct CcAudio {
    CcAudioDescription description;
    AudioQueueRef queue;
    CFStringRef device_uid;
    AudioSlot slots[64];
    size_t byte_count;
    unsigned next_slot;
    atomic_uint_fast64_t completed_frames;
    atomic_bool format_changed;
    CcAudioCompletion completion;
    void *completion_context;
    bool submitted;
};

static bool native_error(char *error, size_t capacity, const char *operation,
                         OSStatus status) {
    char message[160];
    snprintf(message, sizeof(message), "CoreAudio %s failed (%d).", operation,
             (int)status);
    cc_error_set(error, capacity, message);
    return false;
}

static bool device_property(AudioObjectID device, AudioObjectPropertySelector selector,
                            AudioObjectPropertyScope scope, void *value, UInt32 *size,
                            char *error, size_t capacity) {
    AudioObjectPropertyAddress property = {selector, scope,
                                           kAudioObjectPropertyElementMain};
    OSStatus status =
        AudioObjectGetPropertyData(device, &property, 0, NULL, size, value);
    return status == noErr || native_error(error, capacity, "device query", status);
}

static bool device_channels(AudioObjectID device, uint32_t *channels, char *error,
                            size_t capacity) {
    AudioObjectPropertyAddress property = {kAudioDevicePropertyStreamConfiguration,
                                           kAudioObjectPropertyScopeOutput,
                                           kAudioObjectPropertyElementMain};
    UInt32 size;
    OSStatus status = AudioObjectGetPropertyDataSize(device, &property, 0, NULL, &size);
    if (status != noErr)
        return native_error(error, capacity, "channel size query", status);
    if (size < offsetof(AudioBufferList, mBuffers) || size > 65536) {
        cc_error_set(error, capacity, "CoreAudio channel layout exceeds bounds.");
        return false;
    }
    AudioBufferList *buffers = malloc(size);
    if (!buffers) {
        cc_error_set(error, capacity, "Cannot allocate native audio channel layout.");
        return false;
    }
    bool okay = false;
    status = AudioObjectGetPropertyData(device, &property, 0, NULL, &size, buffers);
    if (status != noErr) {
        native_error(error, capacity, "channel query", status);
        goto release_layout;
    }
    if (size < offsetof(AudioBufferList, mBuffers) ||
        buffers->mNumberBuffers >
            (size - offsetof(AudioBufferList, mBuffers)) / sizeof(AudioBuffer)) {
        cc_error_set(error, capacity, "CoreAudio channel layout is truncated.");
        goto release_layout;
    }
    uint32_t count = 0;
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        if (buffers->mBuffers[index].mNumberChannels > 32 - count) {
            cc_error_set(error, capacity,
                         "CoreAudio device has unsupported channel count.");
            goto release_layout;
        }
        count += buffers->mBuffers[index].mNumberChannels;
    }
    *channels = count;
    okay = true;

release_layout:
    free(buffers);
    return okay;
}

static bool select_device(CcAudio *audio, char *error, size_t capacity) {
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    if (!device_property(
            kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal, &device, &size, error, capacity))
        return false;
    if (device == kAudioObjectUnknown) {
        cc_error_set(error, capacity, "No native audio output device is available.");
        return false;
    }
    Float64 rate = 0;
    size = sizeof(rate);
    uint32_t channels;
    if (!device_property(device, kAudioDevicePropertyNominalSampleRate,
                         kAudioObjectPropertyScopeGlobal, &rate, &size, error,
                         capacity) ||
        !device_channels(device, &channels, error, capacity))
        return false;
    if (rate != audio->description.sample_rate ||
        channels != audio->description.channels) {
        cc_error_set(
            error, capacity,
            "Native device rate or channel count differs from requested PCM stream.");
        return false;
    }
    size = sizeof(audio->device_uid);
    return device_property(device, kAudioDevicePropertyDeviceUID,
                           kAudioObjectPropertyScopeGlobal, &audio->device_uid, &size,
                           error, capacity) &&
           audio->device_uid;
}

static void complete_buffer(void *context, AudioQueueRef queue,
                            AudioQueueBufferRef buffer) {
    CcAudio *audio = context;
    (void)queue;
    for (unsigned index = 0; index < audio->description.buffer_count; ++index) {
        AudioSlot *slot = &audio->slots[index];
        if (slot->buffer != buffer)
            continue;
        if (atomic_exchange_explicit(&slot->queued, false, memory_order_acq_rel)) {
            uint64_t completed =
                atomic_fetch_add_explicit(&audio->completed_frames,
                                          audio->description.frames_per_buffer,
                                          memory_order_relaxed) +
                audio->description.frames_per_buffer;
            if (audio->completion)
                audio->completion(audio->completion_context, completed);
        }
        return;
    }
}

static void format_changed(void *context, AudioQueueRef queue,
                           AudioQueuePropertyID property) {
    CcAudio *audio = context;
    (void)queue;
    (void)property;
    atomic_store_explicit(&audio->format_changed, true, memory_order_release);
}

static bool observe_format(CcAudio *audio, char *error, size_t capacity) {
    static const AudioQueuePropertyID properties[3] = {
        kAudioQueueDeviceProperty_SampleRate, kAudioQueueDeviceProperty_NumberChannels,
        kAudioQueueProperty_CurrentDevice};
    for (unsigned index = 0; index < 3; ++index) {
        OSStatus status = AudioQueueAddPropertyListener(audio->queue, properties[index],
                                                        format_changed, audio);
        if (status != noErr)
            return native_error(error, capacity, "format observer", status);
    }
    return true;
}

static bool unchanged_format(const CcAudio *audio, char *error, size_t capacity) {
    if (!atomic_load_explicit(&audio->format_changed, memory_order_acquire))
        return true;
    cc_error_set(error, capacity,
                 "Native audio format changed; recreate the PCM queue.");
    return false;
}

static bool bind_channels(CcAudio *audio, char *error, size_t capacity) {
    OSStatus status =
        AudioQueueSetProperty(audio->queue, kAudioQueueProperty_CurrentDevice,
                              &audio->device_uid, sizeof(audio->device_uid));
    if (status != noErr)
        return native_error(error, capacity, "device binding", status);
    if (__builtin_available(macOS 10.15, *)) {
        AudioQueueChannelAssignment assignments[32];
        for (unsigned channel = 0; channel < audio->description.channels; ++channel)
            assignments[channel] =
                (AudioQueueChannelAssignment){audio->device_uid, channel + 1};
        status = AudioQueueSetProperty(
            audio->queue, kAudioQueueProperty_ChannelAssignments, assignments,
            audio->description.channels * sizeof(*assignments));
    } else {
        cc_error_set(error, capacity, "Native channel binding requires macOS 10.15.");
        return false;
    }
    if (status != noErr)
        return native_error(error, capacity, "channel binding", status);
    Float64 rate;
    UInt32 channels;
    UInt32 size = sizeof(rate);
    status = AudioQueueGetProperty(audio->queue, kAudioQueueDeviceProperty_SampleRate,
                                   &rate, &size);
    if (status != noErr)
        return native_error(error, capacity, "queue rate query", status);
    size = sizeof(channels);
    status = AudioQueueGetProperty(
        audio->queue, kAudioQueueDeviceProperty_NumberChannels, &channels, &size);
    if (status != noErr)
        return native_error(error, capacity, "queue channel query", status);
    if (rate != audio->description.sample_rate ||
        channels != audio->description.channels) {
        cc_error_set(error, capacity,
                     "Native queue device format changed during creation.");
        return false;
    }
    status = AudioQueueSetParameter(audio->queue, kAudioQueueParam_Volume, 1.0f);
    return status == noErr || native_error(error, capacity, "unity gain", status);
}

CcAudio *cc_audio_create(const CcAudioDescription *description, char *error,
                         size_t error_capacity) {
    size_t byte_count;
    if (!cc_audio_description_validate(description, &byte_count, error, error_capacity))
        return NULL;
    CcAudio *audio = calloc(1, sizeof(*audio));
    if (!audio) {
        cc_error_set(error, error_capacity, "Cannot allocate native audio queue.");
        return NULL;
    }
    audio->description = *description;
    audio->byte_count = byte_count;
    atomic_init(&audio->completed_frames, 0);
    atomic_init(&audio->format_changed, false);
    for (unsigned index = 0; index < description->buffer_count; ++index)
        atomic_init(&audio->slots[index].queued, false);
    if (!select_device(audio, error, error_capacity))
        goto release_audio;
    UInt32 bytes_per_frame = description->channels * (UInt32)sizeof(float);
    AudioStreamBasicDescription format = {.mSampleRate = description->sample_rate,
                                          .mFormatID = kAudioFormatLinearPCM,
                                          .mFormatFlags = kAudioFormatFlagIsFloat |
                                                          kAudioFormatFlagIsPacked |
                                                          kAudioFormatFlagsNativeEndian,
                                          .mBytesPerPacket = bytes_per_frame,
                                          .mFramesPerPacket = 1,
                                          .mBytesPerFrame = bytes_per_frame,
                                          .mChannelsPerFrame = description->channels,
                                          .mBitsPerChannel = 32};
    OSStatus status = AudioQueueNewOutput(&format, complete_buffer, audio, NULL, NULL,
                                          0, &audio->queue);
    if (status != noErr) {
        native_error(error, error_capacity, "queue creation", status);
        goto release_audio;
    }
    if (!bind_channels(audio, error, error_capacity) ||
        !observe_format(audio, error, error_capacity))
        goto release_audio;
    for (unsigned index = 0; index < description->buffer_count; ++index) {
        status = AudioQueueAllocateBuffer(audio->queue, (UInt32)byte_count,
                                          &audio->slots[index].buffer);
        if (status != noErr) {
            native_error(error, error_capacity, "buffer allocation", status);
            goto release_audio;
        }
        if (!audio->slots[index].buffer ||
            audio->slots[index].buffer->mAudioDataBytesCapacity < byte_count) {
            cc_error_set(error, error_capacity,
                         "Native queue allocated a short buffer.");
            goto release_audio;
        }
    }
    return audio;

release_audio:
    cc_audio_destroy(audio);
    return NULL;
}

void cc_audio_destroy(CcAudio *audio) {
    if (!audio)
        return;
    if (audio->queue)
        AudioQueueDispose(audio->queue, true);
    if (audio->device_uid)
        CFRelease(audio->device_uid);
    free(audio);
}

bool cc_audio_set_completion(CcAudio *audio, CcAudioCompletion completion,
                             void *context, char *error, size_t error_capacity) {
    if (!audio || !completion || audio->completion || audio->submitted) {
        cc_error_set(error, error_capacity,
                     "Audio completion requires an unused queue and one observer.");
        return false;
    }
    audio->completion = completion;
    audio->completion_context = context;
    return true;
}

CcAudioWriteResult cc_audio_write(CcAudio *audio, const float *samples,
                                  size_t sample_count, char *error,
                                  size_t error_capacity) {
    if (!audio || !samples || sample_count != audio->byte_count / sizeof(float)) {
        cc_error_set(error, error_capacity,
                     "Audio write requires one complete PCM buffer.");
        return CC_AUDIO_INVALID;
    }
    if (!unchanged_format(audio, error, error_capacity))
        return CC_AUDIO_DEVICE_ERROR;
    for (size_t index = 0; index < sample_count; ++index) {
        if (!isfinite(samples[index])) {
            cc_error_set(error, error_capacity,
                         "PCM buffer contains a nonfinite sample.");
            return CC_AUDIO_INVALID;
        }
    }
    for (unsigned attempt = 0; attempt < audio->description.buffer_count; ++attempt) {
        unsigned index = (audio->next_slot + attempt) % audio->description.buffer_count;
        AudioSlot *slot = &audio->slots[index];
        bool expected = false;
        if (!atomic_compare_exchange_strong_explicit(&slot->queued, &expected, true,
                                                     memory_order_acq_rel,
                                                     memory_order_relaxed))
            continue;
        memcpy(slot->buffer->mAudioData, samples, audio->byte_count);
        slot->buffer->mAudioDataByteSize = (UInt32)audio->byte_count;
        audio->submitted = true;
        OSStatus status = AudioQueueEnqueueBuffer(audio->queue, slot->buffer, 0, NULL);
        if (status != noErr) {
            atomic_store_explicit(&slot->queued, false, memory_order_release);
            native_error(error, error_capacity, "buffer enqueue", status);
            return CC_AUDIO_DEVICE_ERROR;
        }
        audio->next_slot = (index + 1) % audio->description.buffer_count;
        return CC_AUDIO_QUEUED;
    }
    return CC_AUDIO_FULL;
}

unsigned cc_audio_writable_buffers(const CcAudio *audio) {
    if (!audio)
        return 0;
    unsigned count = 0;
    for (unsigned index = 0; index < audio->description.buffer_count; ++index) {
        if (!atomic_load_explicit(&audio->slots[index].queued, memory_order_acquire))
            ++count;
    }
    return count;
}

uint64_t cc_audio_completed_frames(const CcAudio *audio) {
    return audio ? atomic_load_explicit(&audio->completed_frames, memory_order_relaxed)
                 : 0;
}

bool cc_audio_start(CcAudio *audio, char *error, size_t error_capacity) {
    if (!audio || cc_audio_writable_buffers(audio) == audio->description.buffer_count) {
        cc_error_set(error, error_capacity, "Audio start requires queued PCM data.");
        return false;
    }
    if (!unchanged_format(audio, error, error_capacity))
        return false;
    OSStatus status = AudioQueueStart(audio->queue, NULL);
    return status == noErr || native_error(error, error_capacity, "start", status);
}

bool cc_audio_stop(CcAudio *audio, char *error, size_t error_capacity) {
    if (!audio) {
        cc_error_set(error, error_capacity, "Audio queue is missing.");
        return false;
    }
    OSStatus status = AudioQueueStop(audio->queue, true);
    if (status != noErr)
        return native_error(error, error_capacity, "stop", status);
    for (unsigned index = 0; index < audio->description.buffer_count; ++index)
        atomic_store_explicit(&audio->slots[index].queued, false, memory_order_release);
    audio->next_slot = 0;
    return true;
}

bool cc_audio_running(CcAudio *audio, bool *running, char *error,
                      size_t error_capacity) {
    if (!audio || !running) {
        cc_error_set(error, error_capacity,
                     "Audio queue or running output is missing.");
        return false;
    }
    UInt32 value;
    UInt32 size = sizeof(value);
    OSStatus status = AudioQueueGetProperty(audio->queue, kAudioQueueProperty_IsRunning,
                                            &value, &size);
    if (status != noErr)
        return native_error(error, error_capacity, "running query", status);
    *running = value != 0;
    return true;
}
