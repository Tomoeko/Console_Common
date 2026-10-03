#include "console_common/capture/capture_writer.h"
#include "capture/capture_audio.h"

#include <AudioToolbox/AudioToolbox.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
} TestBox;

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint64_t read_u64(const uint8_t *bytes) {
    return (uint64_t)read_u32(bytes) << 32 | read_u32(bytes + 4);
}

static TestBox find_box(const uint8_t *bytes, size_t size, const char type[4]) {
    size_t offset = 0;
    while (size - offset >= 8) {
        uint64_t length = read_u32(bytes + offset);
        size_t header = 8;
        if (length == 1) {
            assert(size - offset >= 16);
            length = read_u64(bytes + offset + 8);
            header = 16;
        }
        assert(length >= header && length <= size - offset);
        if (!memcmp(bytes + offset + 4, type, 4))
            return (TestBox){bytes + offset, (size_t)length};
        offset += (size_t)length;
    }
    return (TestBox){0};
}

static TestBox child_box(TestBox parent, const char type[4]) {
    assert(parent.bytes && parent.size >= 8);
    TestBox child = find_box(parent.bytes + 8, parent.size - 8, type);
    assert(child.bytes);
    return child;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

static void check_metadata(const char *path, uint32_t sample_rate, size_t input_frames,
                           const CcCaptureAudioConfig *config) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    TestBox movie = find_box(bytes, size, "moov");
    assert(movie.bytes);
    TestBox movie_header = child_box(movie, "mvhd");
    uint32_t movie_timescale = read_u32(movie_header.bytes + 28);
    assert(movie_timescale && movie_timescale % sample_rate == 0);
    TestBox track = child_box(movie, "trak");
    TestBox media = child_box(track, "mdia");
    TestBox handler = child_box(media, "hdlr");
    if (memcmp(handler.bytes + 16, "soun", 4)) {
        const uint8_t *next = track.bytes + track.size;
        track = find_box(next, movie.size - (size_t)(next - movie.bytes), "trak");
        media = child_box(track, "mdia");
        handler = child_box(media, "hdlr");
    }
    assert(!memcmp(handler.bytes + 16, "soun", 4));
    TestBox header = child_box(media, "mdhd");
    assert(header.bytes[8] == 1 && read_u32(header.bytes + 28) == sample_rate);
    TestBox edit = child_box(child_box(track, "edts"), "elst");
    assert(edit.bytes[8] == 1 && read_u32(edit.bytes + 12) == 1);
    uint64_t expected_duration = (uint64_t)input_frames * movie_timescale / sample_rate;
    assert(read_u64(edit.bytes + 16) == expected_duration);
    assert(read_u64(edit.bytes + 24) == config->priming_frames);
    TestBox track_header = child_box(track, "tkhd");
    assert(read_u64(track_header.bytes + 36) == expected_duration);
    TestBox table = child_box(child_box(media, "minf"), "stbl");
    TestBox sizes = child_box(table, "stsz");
    TestBox times = child_box(table, "stts");
    TestBox chunks = child_box(table, "stsc");
    TestBox offsets = child_box(table, "co64");
    TestBox roll = child_box(table, "sgpd");
    TestBox groups = child_box(table, "sbgp");
    assert(roll.size == 26 && roll.bytes[8] == 1 &&
           !memcmp(roll.bytes + 12, "roll", 4) && read_u32(roll.bytes + 16) == 2 &&
           read_u32(roll.bytes + 20) == 1 && roll.bytes[24] == 255 &&
           roll.bytes[25] == 255);
    assert(read_u32(sizes.bytes + 12) == 0);
    uint32_t packets = read_u32(sizes.bytes + 16);
    assert(packets && read_u32(offsets.bytes + 12) == packets);
    assert(groups.size == 28 && !memcmp(groups.bytes + 12, "roll", 4) &&
           read_u32(groups.bytes + 16) == 1 && read_u32(groups.bytes + 20) == packets &&
           read_u32(groups.bytes + 24) == 1);
    assert(read_u32(times.bytes + 12) == 1 && read_u32(times.bytes + 16) == packets &&
           read_u32(times.bytes + 20) == config->packet_frames);
    assert(read_u32(chunks.bytes + 12) == 1 && read_u32(chunks.bytes + 16) == 1 &&
           read_u32(chunks.bytes + 20) == 1);
    uint64_t coded_frames = (uint64_t)packets * config->packet_frames;
    assert(read_u64(header.bytes + 32) == coded_frames);
    assert(coded_frames >= input_frames + config->priming_frames);
    assert(coded_frames - input_frames - config->priming_frames <
           config->packet_frames);
    for (uint32_t packet = 0; packet < packets; ++packet) {
        uint32_t packet_bytes = read_u32(sizes.bytes + 20 + (size_t)packet * 4);
        uint64_t offset = read_u64(offsets.bytes + 16 + (size_t)packet * 8);
        assert(packet_bytes && offset < size && packet_bytes <= size - offset);
    }
    TestBox descriptions = child_box(table, "stsd");
    assert(read_u32(descriptions.bytes + 12) == 1);
    TestBox entry = find_box(descriptions.bytes + 16, descriptions.size - 16, "mp4a");
    assert(entry.bytes && entry.size > 36 &&
           read_u32(entry.bytes + 32) == sample_rate << 16);
    TestBox descriptor = find_box(entry.bytes + 36, entry.size - 36, "esds");
    assert(descriptor.bytes && descriptor.size == 39);
    assert(descriptor.bytes[12] == 3 && descriptor.bytes[13] == 25 &&
           descriptor.bytes[17] == 4 && descriptor.bytes[18] == 17 &&
           descriptor.bytes[19] == 0x40 && descriptor.bytes[20] == 0x15);
    assert(descriptor.bytes[32] == 5 && descriptor.bytes[33] == 2 &&
           !memcmp(descriptor.bytes + 34, config->decoder_config, 2));
    TestBox user_data = child_box(movie, "udta");
    TestBox metadata = child_box(user_data, "meta");
    TestBox fields = find_box(metadata.bytes + 12, metadata.size - 12, "ilst");
    TestBox field = child_box(fields, "----");
    TestBox owner = child_box(field, "mean");
    TestBox name = child_box(field, "name");
    TestBox data = child_box(field, "data");
    assert(owner.size == 28 && !memcmp(owner.bytes + 12, "com.apple.iTunes", 16));
    assert(name.size == 20 && !memcmp(name.bytes + 12, "iTunSMPB", 8));
    assert(data.size == 132);
    char timing[117];
    memcpy(timing, data.bytes + 16, sizeof(timing) - 1);
    timing[sizeof(timing) - 1] = '\0';
    unsigned reserved, priming, padding;
    unsigned long long valid;
    assert(sscanf(timing, " %x %x %x %llx", &reserved, &priming, &padding, &valid) ==
           4);
    assert(reserved == 0 && priming == config->priming_frames && valid == input_frames);
    assert(padding == coded_frames - input_frames - config->priming_frames);
    free(bytes);
}

static void check_decoded(const char *path, uint32_t sample_rate, const float *source,
                          size_t input_frames) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    assert(url);
    ExtAudioFileRef file;
    assert(ExtAudioFileOpenURL(url, &file) == noErr);
    CFRelease(url);
    SInt64 decoded_length;
    UInt32 size = sizeof(decoded_length);
    assert(ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size,
                                   &decoded_length) == noErr);
    /* The MP4 audio-file reader ignores presentation edits; it may expose the
     * complete last AAC packet. Metadata is checked separately above. */
    assert(decoded_length >= (SInt64)input_frames &&
           decoded_length - (SInt64)input_frames < 1024);
    AudioStreamBasicDescription format = {.mSampleRate = sample_rate,
                                          .mFormatID = kAudioFormatLinearPCM,
                                          .mFormatFlags = kAudioFormatFlagIsFloat |
                                                          kAudioFormatFlagIsPacked,
                                          .mBytesPerPacket = sizeof(float) * 2,
                                          .mFramesPerPacket = 1,
                                          .mBytesPerFrame = sizeof(float) * 2,
                                          .mChannelsPerFrame = 2,
                                          .mBitsPerChannel = 32};
    assert(ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat,
                                   sizeof(format), &format) == noErr);
    float decoded[2048 * 2];
    size_t frames = 0;
    double squared_error = 0;
    double signal_energy = 0;
    for (;;) {
        AudioBufferList buffers = {.mNumberBuffers = 1,
                                   .mBuffers = {{2, sizeof(decoded), decoded}}};
        UInt32 count = 2048;
        assert(ExtAudioFileRead(file, &count, &buffers) == noErr);
        if (!count)
            break;
        assert(count <= 2048 && count <= (size_t)decoded_length - frames);
        size_t valid = frames < input_frames ? input_frames - frames : 0;
        if (valid > count)
            valid = count;
        for (size_t sample = 0; sample < valid * 2; ++sample) {
            double input = source[frames * 2 + sample];
            double difference = decoded[sample] - input;
            squared_error += difference * difference;
            signal_energy += input * input;
        }
        frames += count;
    }
    assert(ExtAudioFileDispose(file) == noErr && frames == (size_t)decoded_length);
    /* Lossy AAC must retain stereo content, timing and more than 30 dB SNR. */
    if (input_frames > sample_rate / 2) {
        assert(signal_energy > 1 && squared_error / signal_energy < 0.001);
    }
}

static void test_recording(const char *directory, uint32_t sample_rate, size_t frames,
                           bool include_video, bool rejected_append) {
    CcCaptureAudio *encoder = cc_capture_audio_open(sample_rate);
    assert(encoder);
    CcCaptureAudioConfig config;
    assert(cc_capture_audio_config(encoder, &config) && config.packet_frames == 1024);
    cc_capture_audio_close(encoder);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/capture-web-%u-%zu-%u.mp4", directory,
                          sample_rate, frames, (unsigned)rejected_append);
    assert(length > 0 && (size_t)length < sizeof(path));
    remove(path);
    CcCaptureWriter *writer = cc_capture_writer_open_with_audio(
        path, 64, 64, 1000000, sample_rate, CC_CAPTURE_AUDIO_WEB);
    assert(writer && !cc_capture_writer_open_with_audio(
                         path, 64, 64, 1000000, sample_rate, CC_CAPTURE_AUDIO_WEB));
    float *source = malloc((frames ? frames : 1) * sizeof(float) * 2);
    assert(source);
    for (size_t frame = 0; frame < frames; ++frame) {
        double envelope = frame < 64 ? (double)frame / 64 : 1;
        source[frame * 2] = (float)(envelope * 0.5 *
                                    sin(frame * 997 * 6.283185307179586 / sample_rate));
        source[frame * 2 + 1] =
            (float)(envelope * 0.35 *
                    sin(frame * 1777 * 6.283185307179586 / sample_rate));
    }
    uint8_t rgba[64 * 64 * 4];
    memset(rgba, 128, sizeof(rgba));
    if (include_video)
        assert(cc_capture_writer_video(writer, rgba, 64 * 4, 1000000));
    static const size_t chunks[] = {1, 31, 1023, 4096, 17, 4095};
    size_t offset = 0;
    size_t chunk = 0;
    while (offset < frames) {
        size_t count = chunks[chunk++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (count > frames - offset)
            count = frames - offset;
        assert(cc_capture_writer_audio(writer, source + offset * 2, count));
        offset += count;
    }
    assert(cc_capture_writer_audio(writer, NULL, 0));
    if (rejected_append)
        assert(!cc_capture_writer_audio(writer, NULL, 1));
    assert(cc_capture_writer_close(writer) == !rejected_append);
    check_metadata(path, sample_rate, frames, &config);
    if (frames)
        check_decoded(path, sample_rate, source, frames);
    free(source);
}

int main(int argc, char **argv) {
    assert(argc == 2 && argv[1][0]);
    assert(cc_capture_audio_mode_supported(CC_CAPTURE_AUDIO_NORMAL));
    if (!cc_capture_audio_mode_supported(CC_CAPTURE_AUDIO_WEB)) {
        fputs("The web recording codecs are unavailable in this build.\n", stderr);
        return 77;
    }
    assert(!cc_capture_audio_mode_supported((CcCaptureAudioMode)2));
    assert(!cc_capture_audio_open(0) && !cc_capture_audio_open(12345));
    assert(!cc_capture_audio_config(NULL, NULL));
    cc_capture_audio_close(NULL);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/capture-web-invalid.mp4", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(path));
    remove(path);
    assert(!cc_capture_writer_open_with_audio(path, 1, 1, 1000000, 48000,
                                              CC_CAPTURE_AUDIO_WEB));
    assert(!cc_capture_writer_open_with_audio(path, 64, 64, 1000000, 48000,
                                              (CcCaptureAudioMode)2));
    test_recording(argv[1], 48000, 48013, true, false);
    test_recording(argv[1], 32000, 32037, false, false);
    test_recording(argv[1], 48000, 1, false, false);
    test_recording(argv[1], 48000, 1024, false, false);
    test_recording(argv[1], 48000, 0, false, false);
    test_recording(argv[1], 48000, 5389, false, true);
    return 0;
}
