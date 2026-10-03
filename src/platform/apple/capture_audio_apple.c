#include "capture/capture_audio.h"

#include <AudioToolbox/AudioToolbox.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CAPTURE_AUDIO_INPUT_FRAMES = 4096,
    CAPTURE_AUDIO_NEEDS_INPUT = 1,
    CAPTURE_AUDIO_MAX_PACKET_BYTES = 65536
};

struct CcCaptureAudio {
    AudioConverterRef converter;
    float input[CAPTURE_AUDIO_INPUT_FRAMES * 2];
    size_t input_frames;
    size_t input_offset;
    uint8_t *packet;
    uint32_t packet_capacity;
    CcCaptureAudioConfig config;
    bool finishing;
    bool finished;
    char error[128];
};

static bool fail_audio(CcCaptureAudio *audio, const char *message, OSStatus status) {
    if (!audio->error[0]) {
        if (status == noErr)
            snprintf(audio->error, sizeof(audio->error), "%s", message);
        else
            snprintf(audio->error, sizeof(audio->error), "%s (status %d)", message,
                     (int)status);
    }
    return false;
}

static int frequency_index(uint32_t sample_rate) {
    static const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                     22050, 16000, 12000, 11025, 8000,  7350};
    for (size_t index = 0; index < sizeof(rates) / sizeof(rates[0]); ++index) {
        if (rates[index] == sample_rate)
            return (int)index;
    }
    return -1;
}

bool cc_capture_audio_available(void) {
    return true;
}

static bool configure_converter(CcCaptureAudio *audio, uint32_t sample_rate) {
    UInt32 bitrate = sample_rate < 42667 ? sample_rate * 6 : 256000;
    UInt32 quality = kAudioConverterQuality_High;
    if (AudioConverterSetProperty(audio->converter, kAudioConverterEncodeBitRate,
                                  sizeof(bitrate), &bitrate) != noErr ||
        AudioConverterSetProperty(audio->converter, kAudioConverterCodecQuality,
                                  sizeof(quality), &quality) != noErr)
        return false;
    AudioStreamBasicDescription output;
    UInt32 size = sizeof(output);
    if (AudioConverterGetProperty(audio->converter,
                                  kAudioConverterCurrentOutputStreamDescription, &size,
                                  &output) != noErr ||
        size != sizeof(output) || output.mFormatID != kAudioFormatMPEG4AAC ||
        (output.mFormatFlags != 0 && output.mFormatFlags != kMPEG4Object_AAC_LC) ||
        output.mSampleRate != sample_rate || output.mChannelsPerFrame != 2 ||
        output.mFramesPerPacket != 1024)
        return false;
    size = sizeof(bitrate);
    if (AudioConverterGetProperty(audio->converter, kAudioConverterEncodeBitRate, &size,
                                  &bitrate) != noErr ||
        size != sizeof(bitrate))
        return false;
    UInt32 packet_size;
    size = sizeof(packet_size);
    if (AudioConverterGetProperty(audio->converter,
                                  kAudioConverterPropertyMaximumOutputPacketSize, &size,
                                  &packet_size) != noErr ||
        size != sizeof(packet_size) || !packet_size ||
        packet_size > CAPTURE_AUDIO_MAX_PACKET_BYTES)
        return false;
    AudioConverterPrimeInfo prime;
    size = sizeof(prime);
    if (AudioConverterGetProperty(audio->converter, kAudioConverterPrimeInfo, &size,
                                  &prime) != noErr ||
        size != sizeof(prime))
        return false;
    audio->packet = malloc(packet_size);
    if (!audio->packet)
        return false;
    audio->packet_capacity = packet_size;
    int index = frequency_index(sample_rate);
    /* AAC-LC object type 2, unchanged sample rate, stereo channel configuration 2. */
    audio->config.decoder_config[0] = (uint8_t)(0x10 | ((unsigned)index >> 1));
    audio->config.decoder_config[1] = (uint8_t)(((unsigned)index & 1) << 7 | 0x10);
    audio->config.bitrate = bitrate;
    audio->config.packet_frames = output.mFramesPerPacket;
    audio->config.priming_frames = prime.leadingFrames;
    return true;
}

CcCaptureAudio *cc_capture_audio_open(uint32_t sample_rate) {
    if (sample_rate < 8000 || sample_rate > 65535 || frequency_index(sample_rate) < 0)
        return NULL;
    CcCaptureAudio *audio = calloc(1, sizeof(*audio));
    if (!audio)
        return NULL;
    AudioStreamBasicDescription input = {.mSampleRate = sample_rate,
                                         .mFormatID = kAudioFormatLinearPCM,
                                         .mFormatFlags = kAudioFormatFlagIsFloat |
                                                         kAudioFormatFlagIsPacked,
                                         .mBytesPerPacket = sizeof(float) * 2,
                                         .mFramesPerPacket = 1,
                                         .mBytesPerFrame = sizeof(float) * 2,
                                         .mChannelsPerFrame = 2,
                                         .mBitsPerChannel = 32};
    AudioStreamBasicDescription output = {.mSampleRate = sample_rate,
                                          .mFormatID = kAudioFormatMPEG4AAC,
                                          .mFormatFlags = kMPEG4Object_AAC_LC,
                                          .mFramesPerPacket = 1024,
                                          .mChannelsPerFrame = 2};
    if (AudioConverterNew(&input, &output, &audio->converter) != noErr ||
        !configure_converter(audio, sample_rate)) {
        cc_capture_audio_close(audio);
        return NULL;
    }
    return audio;
}

static OSStatus supply_audio(AudioConverterRef converter, UInt32 *packet_count,
                             AudioBufferList *buffers,
                             AudioStreamPacketDescription **descriptions,
                             void *context) {
    (void)converter;
    (void)descriptions;
    CcCaptureAudio *audio = context;
    size_t remaining = audio->input_frames - audio->input_offset;
    size_t count = remaining < *packet_count ? remaining : *packet_count;
    buffers->mNumberBuffers = 1;
    buffers->mBuffers[0].mNumberChannels = 2;
    buffers->mBuffers[0].mDataByteSize = (UInt32)(count * sizeof(float) * 2);
    buffers->mBuffers[0].mData = count ? audio->input + audio->input_offset * 2 : NULL;
    *packet_count = (UInt32)count;
    audio->input_offset += count;
    /* A temporary empty buffer preserves encoder state between queued jobs.
     * Only finish returns zero packets with noErr, signalling end of stream. */
    return count || audio->finishing ? noErr : CAPTURE_AUDIO_NEEDS_INPUT;
}

static bool encode_available(CcCaptureAudio *audio, CcCaptureAudioPacket emit,
                             void *context) {
    for (;;) {
        AudioBufferList output = {
            .mNumberBuffers = 1,
            .mBuffers = {{2, audio->packet_capacity, audio->packet}}};
        AudioStreamPacketDescription description = {0};
        UInt32 packets = 1;
        OSStatus status = AudioConverterFillComplexBuffer(
            audio->converter, supply_audio, audio, &packets, &output, &description);
        if (status != noErr && status != CAPTURE_AUDIO_NEEDS_INPUT)
            return fail_audio(audio, "AAC encoding failed", status);
        if (packets) {
            if (packets != 1 || description.mStartOffset < 0 ||
                description.mStartOffset > audio->packet_capacity ||
                !description.mDataByteSize ||
                description.mDataByteSize >
                    audio->packet_capacity - (uint32_t)description.mStartOffset ||
                (description.mVariableFramesInPacket &&
                 description.mVariableFramesInPacket != audio->config.packet_frames))
                return fail_audio(audio, "AAC encoder returned an invalid packet",
                                  noErr);
            if (!emit(context, audio->packet + description.mStartOffset,
                      description.mDataByteSize, audio->config.packet_frames))
                return fail_audio(audio, "Cannot append the AAC packet", noErr);
        }
        if (status == CAPTURE_AUDIO_NEEDS_INPUT)
            return audio->input_offset == audio->input_frames ||
                   fail_audio(audio, "AAC encoder left unread input", noErr);
        if (!packets) {
            if (!audio->finishing)
                return fail_audio(audio, "AAC encoder stopped before end of stream",
                                  noErr);
            return true;
        }
    }
}

bool cc_capture_audio_append(CcCaptureAudio *audio, const float *stereo,
                             size_t frame_count, CcCaptureAudioPacket emit,
                             void *context) {
    if (!audio || !emit || audio->finished || audio->error[0] ||
        (frame_count && !stereo) || frame_count > SIZE_MAX / (sizeof(float) * 2))
        return false;
    while (frame_count) {
        size_t count = frame_count < CAPTURE_AUDIO_INPUT_FRAMES
                           ? frame_count
                           : CAPTURE_AUDIO_INPUT_FRAMES;
        memcpy(audio->input, stereo, count * sizeof(float) * 2);
        audio->input_frames = count;
        audio->input_offset = 0;
        if (!encode_available(audio, emit, context))
            return false;
        stereo += count * 2;
        frame_count -= count;
    }
    return true;
}

bool cc_capture_audio_finish(CcCaptureAudio *audio, CcCaptureAudioPacket emit,
                             void *context) {
    if (!audio || !emit || audio->error[0])
        return false;
    if (audio->finished)
        return true;
    audio->finishing = true;
    audio->input_frames = audio->input_offset = 0;
    bool okay = encode_available(audio, emit, context);
    if (okay) {
        AudioConverterPrimeInfo prime;
        UInt32 size = sizeof(prime);
        OSStatus status = AudioConverterGetProperty(
            audio->converter, kAudioConverterPrimeInfo, &size, &prime);
        if (status != noErr || size != sizeof(prime))
            okay = fail_audio(audio, "Cannot read final AAC priming", status);
        else
            audio->config.priming_frames = prime.leadingFrames;
    }
    audio->finished = true;
    return okay;
}

bool cc_capture_audio_config(const CcCaptureAudio *audio,
                             CcCaptureAudioConfig *config) {
    if (!audio || !config || audio->error[0])
        return false;
    *config = audio->config;
    return true;
}

const char *cc_capture_audio_error(const CcCaptureAudio *audio) {
    return audio ? audio->error : "AAC encoder is not available.";
}

void cc_capture_audio_close(CcCaptureAudio *audio) {
    if (!audio)
        return;
    if (audio->converter)
        AudioConverterDispose(audio->converter);
    free(audio->packet);
    free(audio);
}
