#define _POSIX_C_SOURCE 200809L
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "console_common/capture/capture_writer.h"
#include "capture_video.h"
#include "capture_audio.h"

#include <float.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

enum {
    CAPTURE_MOVIE_TIMESCALE = 1000000,
    CAPTURE_AUDIO_FRAME_BYTES = 8,
    CAPTURE_INITIAL_INDEX_CAPACITY = 256,
    CAPTURE_AUDIO_BUFFER_BYTES = 8192
};

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    bool keyframe;
} CaptureSample;

typedef struct {
    CaptureSample *samples;
    size_t count;
    size_t capacity;
    uint64_t duration;
} CaptureTrack;

struct CcCaptureWriter {
    FILE *file;
    CcCaptureVideo *encoder;
    CcCaptureAudio *audio_encoder;
    CcCaptureAudioConfig audio_config;
    CaptureTrack video;
    CaptureTrack audio;
    uint64_t position;
    uint64_t complete_position;
    uint64_t mdat_position;
    uint32_t video_timescale;
    uint32_t movie_timescale;
    uint32_t audio_rate;
    uint64_t audio_input_frames;
    unsigned width;
    unsigned height;
    const char *error;
    bool io_failed;
};

static bool fail_writer(CcCaptureWriter *writer, const char *message) {
    if (!writer->error)
        writer->error = message;
    return false;
}

static bool seek_file(FILE *file, uint64_t position) {
    if (position > INT64_MAX)
        return false;
#if defined(_WIN32)
    return _fseeki64(file, (__int64)position, SEEK_SET) == 0;
#else
    off_t converted = (off_t)position;
    return converted >= 0 && (uint64_t)converted == position &&
           fseeko(file, converted, SEEK_SET) == 0;
#endif
}

static bool truncate_file(FILE *file, uint64_t size) {
    if (size > INT64_MAX || fflush(file) != 0)
        return false;
#if defined(_WIN32)
    return _chsize_s(_fileno(file), size) == 0;
#else
    off_t converted = (off_t)size;
    return converted >= 0 && (uint64_t)converted == size &&
           ftruncate(fileno(file), converted) == 0;
#endif
}

static void write_bytes(CcCaptureWriter *writer, const void *bytes, size_t count) {
    if (writer->io_failed)
        return;
    if (count > INT64_MAX - writer->position ||
        fwrite(bytes, 1, count, writer->file) != count) {
        writer->io_failed = true;
        fail_writer(writer, "Cannot write recording data.");
        return;
    }
    writer->position += count;
}

static void write_u8(CcCaptureWriter *writer, uint8_t value) {
    write_bytes(writer, &value, sizeof(value));
}

static void write_u16(CcCaptureWriter *writer, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    write_bytes(writer, bytes, sizeof(bytes));
}

static void write_u32(CcCaptureWriter *writer, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    write_bytes(writer, bytes, sizeof(bytes));
}

static void write_u64(CcCaptureWriter *writer, uint64_t value) {
    write_u32(writer, (uint32_t)(value >> 32));
    write_u32(writer, (uint32_t)value);
}

static void write_zeros(CcCaptureWriter *writer, size_t count) {
    static const uint8_t zeros[64] = {0};
    while (count) {
        size_t part = count < sizeof(zeros) ? count : sizeof(zeros);
        write_bytes(writer, zeros, part);
        count -= part;
    }
}

static uint64_t begin_box(CcCaptureWriter *writer, const char type[4]) {
    uint64_t start = writer->position;
    write_u32(writer, 0);
    write_bytes(writer, type, 4);
    return start;
}

static void end_box(CcCaptureWriter *writer, uint64_t start) {
    if (writer->io_failed)
        return;
    uint64_t end = writer->position;
    if (end < start || end - start > UINT32_MAX || !seek_file(writer->file, start)) {
        writer->io_failed = true;
        fail_writer(writer, "Recording index exceeds its file format limits.");
        return;
    }
    writer->position = start;
    write_u32(writer, (uint32_t)(end - start));
    if (!writer->io_failed && !seek_file(writer->file, end)) {
        writer->io_failed = true;
        fail_writer(writer, "Cannot finalize recording index.");
    }
    writer->position = end;
}

static uint64_t begin_full_box(CcCaptureWriter *writer, const char type[4],
                               uint32_t version_flags) {
    uint64_t start = begin_box(writer, type);
    write_u32(writer, version_flags);
    return start;
}

static bool reserve_sample(CcCaptureWriter *writer, CaptureTrack *track) {
    /* stsz, stts and co64 are 32-bit-count tables inside 32-bit-sized boxes. */
    if (writer->video.count + writer->audio.count >= (UINT32_MAX - 2048u) / 32u)
        return fail_writer(writer, "Recording has too many indexed samples.");
    if (track->count < track->capacity)
        return true;
    size_t capacity =
        track->capacity ? track->capacity * 2 : CAPTURE_INITIAL_INDEX_CAPACITY;
    if (capacity < track->capacity || capacity > SIZE_MAX / sizeof(CaptureSample))
        return fail_writer(writer, "Recording index size overflow.");
    CaptureSample *samples = realloc(track->samples, capacity * sizeof(*samples));
    if (!samples)
        return fail_writer(writer, "Cannot allocate recording index.");
    track->samples = samples;
    track->capacity = capacity;
    return true;
}

static void commit_sample(CcCaptureWriter *writer, CaptureTrack *track, uint64_t offset,
                          uint32_t size, uint32_t duration, bool keyframe) {
    track->samples[track->count++] = (CaptureSample){offset, size, duration, keyframe};
    track->duration += duration;
    writer->complete_position = writer->position;
}

static void write_file_header(CcCaptureWriter *writer) {
    uint64_t box = begin_box(writer, "ftyp");
    write_bytes(writer, "isom", 4);
    write_u32(writer, 0x200);
    write_bytes(writer, "isomiso6mp41avc1", 16);
    end_box(writer, box);
    writer->mdat_position = writer->position;
    write_u32(writer, 1); /* Extended size supports captures larger than 4 GiB. */
    write_bytes(writer, "mdat", 4);
    write_u64(writer, 16);
    writer->complete_position = writer->position;
}

static bool movie_duration(uint64_t ticks, uint32_t timescale, uint32_t movie_timescale,
                           uint64_t *duration) {
    uint64_t seconds = ticks / timescale;
    uint64_t remainder = ticks % timescale;
    uint64_t tail = (remainder * movie_timescale + timescale - 1) / timescale;
    if (seconds > (UINT64_MAX - tail) / movie_timescale)
        return false;
    *duration = seconds * movie_timescale + tail;
    return true;
}

static uint32_t audio_movie_timescale(uint32_t sample_rate) {
    /* Retain microsecond video timing and exact audio-frame edit boundaries. */
    uint32_t first = CAPTURE_MOVIE_TIMESCALE;
    uint32_t second = sample_rate;
    while (second) {
        uint32_t remainder = first % second;
        first = second;
        second = remainder;
    }
    uint64_t multiple = (uint64_t)CAPTURE_MOVIE_TIMESCALE / first * sample_rate;
    return multiple <= UINT32_MAX ? (uint32_t)multiple : 0;
}

bool cc_capture_audio_mode_supported(CcCaptureAudioMode mode) {
    return mode == CC_CAPTURE_AUDIO_NORMAL ||
           (mode == CC_CAPTURE_AUDIO_WEB && cc_capture_audio_available() &&
            cc_capture_video_web_available());
}

CcCaptureWriter *cc_capture_writer_open_with_audio(const char *path, unsigned width,
                                                   unsigned height,
                                                   uint32_t video_timescale,
                                                   uint32_t audio_rate,
                                                   CcCaptureAudioMode audio_mode) {
    if (!path || !path[0] || !width || !height || width > 4096 || height > 4096 ||
        !video_timescale || video_timescale > INT32_MAX || !audio_rate ||
        audio_rate > 65535 || sizeof(float) != 4 || FLT_RADIX != 2 ||
        FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128 ||
        !cc_capture_audio_mode_supported(audio_mode))
        return NULL;
    CcCaptureWriter *writer = calloc(1, sizeof(*writer));
    if (!writer)
        return NULL;
    writer->video_timescale = video_timescale;
    writer->movie_timescale = CAPTURE_MOVIE_TIMESCALE;
    writer->audio_rate = audio_rate;
    writer->width = width;
    writer->height = height;
    if (audio_mode == CC_CAPTURE_AUDIO_WEB) {
        writer->movie_timescale = audio_movie_timescale(audio_rate);
        if (!writer->movie_timescale)
            goto release_writer;
        writer->audio_encoder = cc_capture_audio_open(audio_rate);
        if (!writer->audio_encoder ||
            !cc_capture_audio_config(writer->audio_encoder, &writer->audio_config))
            goto release_writer;
    }
    writer->encoder = cc_capture_video_open(width, height, video_timescale);
    if (!writer->encoder)
        goto release_writer;
    if (audio_mode == CC_CAPTURE_AUDIO_WEB) {
        CcCaptureVideoConfig video_config;
        if (!cc_capture_video_config(writer->encoder, &video_config) ||
            video_config.codec != CC_CAPTURE_VIDEO_H264)
            goto release_writer;
    }
    writer->file = fopen(path, "wbx");
    if (!writer->file)
        goto release_writer;
    write_file_header(writer);
    if (writer->error)
        goto release_writer;
    return writer;

release_writer:
    if (writer->file) {
        fclose(writer->file);
        /* Exclusive creation means this failed header cannot belong to an
         * existing recording. No caller can finalize an unsuccessful open. */
        remove(path);
    }
    cc_capture_video_close(writer->encoder);
    cc_capture_audio_close(writer->audio_encoder);
    free(writer);
    return NULL;
}

CcCaptureWriter *cc_capture_writer_open(const char *path, unsigned width,
                                        unsigned height, uint32_t video_timescale,
                                        uint32_t audio_rate) {
    return cc_capture_writer_open_with_audio(path, width, height, video_timescale,
                                             audio_rate, CC_CAPTURE_AUDIO_NORMAL);
}

bool cc_capture_writer_video(CcCaptureWriter *writer, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks) {
    if (!writer || writer->error)
        return false;
    if (!duration_ticks || writer->video.duration > UINT64_MAX - duration_ticks)
        return fail_writer(writer, "Invalid recording frame duration.");
    uint64_t movie_ticks;
    if (!movie_duration(writer->video.duration + duration_ticks,
                        writer->video_timescale, writer->movie_timescale, &movie_ticks))
        return fail_writer(writer, "Recording duration exceeds file format limits.");
    const uint8_t *sample = NULL;
    size_t size = 0;
    bool keyframe = false;
    if (!rgba || row_stride < (size_t)writer->width * 4 ||
        row_stride > SIZE_MAX / writer->height)
        return fail_writer(writer, "Invalid recording video frame.");
    if (!cc_capture_video_encode(writer->encoder, rgba, row_stride, duration_ticks,
                                 &sample, &size, &keyframe)) {
        const char *reason = cc_capture_video_error(writer->encoder);
        return fail_writer(writer, reason[0] ? reason : "Video compression failed.");
    }
    if (!sample || !size || size > UINT32_MAX)
        return fail_writer(writer, "Video compressor returned an invalid sample.");
    if (!writer->video.count && !keyframe)
        return fail_writer(writer, "Recording must begin with an independent frame.");
    if (!reserve_sample(writer, &writer->video))
        return false;
    uint64_t offset = writer->position;
    write_bytes(writer, sample, size);
    if (writer->io_failed)
        return false;
    commit_sample(writer, &writer->video, offset, (uint32_t)size, duration_ticks,
                  keyframe);
    return true;
}

static void write_audio_words(CcCaptureWriter *writer, const float *stereo,
                              size_t count) {
    uint8_t bytes[CAPTURE_AUDIO_BUFFER_BYTES];
    while (count && !writer->io_failed) {
        size_t words = count < sizeof(bytes) / 4 ? count : sizeof(bytes) / 4;
        for (size_t i = 0; i < words; ++i) {
            uint32_t word;
            memcpy(&word, stereo + i, sizeof(word));
            bytes[i * 4] = (uint8_t)word;
            bytes[i * 4 + 1] = (uint8_t)(word >> 8);
            bytes[i * 4 + 2] = (uint8_t)(word >> 16);
            bytes[i * 4 + 3] = (uint8_t)(word >> 24);
        }
        write_bytes(writer, bytes, words * 4);
        stereo += words;
        count -= words;
    }
}

static bool append_audio_packet(void *context, const uint8_t *bytes, size_t size,
                                uint32_t duration) {
    CcCaptureWriter *writer = context;
    if (!bytes || !size || size > UINT32_MAX || !duration ||
        writer->audio.duration > UINT64_MAX - duration ||
        !reserve_sample(writer, &writer->audio))
        return fail_writer(writer, "Invalid encoded recording audio packet.");
    uint64_t offset = writer->position;
    write_bytes(writer, bytes, size);
    if (writer->io_failed)
        return false;
    commit_sample(writer, &writer->audio, offset, (uint32_t)size, duration, true);
    return true;
}

bool cc_capture_writer_audio(CcCaptureWriter *writer, const float *stereo,
                             size_t frame_count) {
    if (!writer || writer->error)
        return false;
    if (!frame_count)
        return true;
    if (!stereo || frame_count > UINT32_MAX / CAPTURE_AUDIO_FRAME_BYTES ||
        frame_count > SIZE_MAX / CAPTURE_AUDIO_FRAME_BYTES ||
        (!writer->audio_encoder && frame_count > UINT32_MAX - writer->audio.duration))
        return fail_writer(writer, "Invalid recording audio frame count.");
    if (writer->audio_encoder) {
        if (frame_count > UINT64_MAX - writer->audio_input_frames)
            return fail_writer(writer, "Recording audio duration overflow.");
        if (!cc_capture_audio_append(writer->audio_encoder, stereo, frame_count,
                                     append_audio_packet, writer))
            return fail_writer(writer, cc_capture_audio_error(writer->audio_encoder));
        writer->audio_input_frames += frame_count;
        return true;
    }
    if (!reserve_sample(writer, &writer->audio))
        return false;
    uint64_t offset = writer->position;
    write_audio_words(writer, stereo, frame_count * 2);
    if (writer->io_failed)
        return false;
    commit_sample(writer, &writer->audio, offset,
                  (uint32_t)frame_count * CAPTURE_AUDIO_FRAME_BYTES,
                  (uint32_t)frame_count, true);
    return true;
}

const char *cc_capture_writer_error(const CcCaptureWriter *writer) {
    return writer && writer->error ? writer->error : "";
}

static void write_identity_matrix(CcCaptureWriter *writer) {
    static const uint32_t matrix[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000};
    for (size_t i = 0; i < sizeof(matrix) / sizeof(matrix[0]); ++i)
        write_u32(writer, matrix[i]);
}

static void write_movie_header(CcCaptureWriter *writer, uint64_t duration) {
    uint64_t box = begin_full_box(writer, "mvhd", 0x1000000);
    write_u64(writer, 0);
    write_u64(writer, 0);
    write_u32(writer, writer->movie_timescale);
    write_u64(writer, duration);
    write_u32(writer, 0x10000);
    write_u16(writer, 0x100);
    write_zeros(writer, 10);
    write_identity_matrix(writer);
    write_zeros(writer, 24);
    write_u32(writer, 3);
    end_box(writer, box);
}

static void write_track_header(CcCaptureWriter *writer, bool video, uint64_t duration) {
    uint64_t box = begin_full_box(writer, "tkhd", 0x1000007);
    write_u64(writer, 0);
    write_u64(writer, 0);
    write_u32(writer, video ? 1 : 2);
    write_u32(writer, 0);
    write_u64(writer, duration);
    write_zeros(writer, 8);
    write_u16(writer, 0);
    write_u16(writer, 0);
    write_u16(writer, video ? 0 : 0x100);
    write_u16(writer, 0);
    write_identity_matrix(writer);
    write_u32(writer, video ? writer->width << 16 : 0);
    write_u32(writer, video ? writer->height << 16 : 0);
    end_box(writer, box);
}

static void write_media_header(CcCaptureWriter *writer, bool video) {
    const CaptureTrack *track = video ? &writer->video : &writer->audio;
    uint64_t box = begin_full_box(writer, "mdhd", 0x1000000);
    write_u64(writer, 0);
    write_u64(writer, 0);
    write_u32(writer, video ? writer->video_timescale : writer->audio_rate);
    write_u64(writer, track->duration);
    write_u16(writer, 0x55c4); /* ISO-639 packed "und": no identifying metadata. */
    write_u16(writer, 0);
    end_box(writer, box);
    box = begin_full_box(writer, "hdlr", 0);
    write_u32(writer, 0);
    write_bytes(writer, video ? "vide" : "soun", 4);
    write_zeros(writer, 12);
    write_u8(writer, 0);
    end_box(writer, box);
}

static void write_data_information(CcCaptureWriter *writer) {
    uint64_t container = begin_box(writer, "dinf");
    uint64_t reference = begin_full_box(writer, "dref", 0);
    write_u32(writer, 1);
    uint64_t url = begin_full_box(writer, "url ", 1);
    end_box(writer, url);
    end_box(writer, reference);
    end_box(writer, container);
}

static bool valid_video_config(const CcCaptureVideoConfig *config) {
    if (config->color_primaries > UINT16_MAX ||
        config->transfer_characteristics > UINT16_MAX ||
        config->matrix_coefficients > UINT16_MAX)
        return false;
    if (config->codec == CC_CAPTURE_VIDEO_JPEG)
        return true;
    return config->codec == CC_CAPTURE_VIDEO_H264 && config->sps && config->pps &&
           config->sps_size >= 4 && config->sps_size <= UINT16_MAX &&
           config->pps_size > 0 && config->pps_size <= UINT16_MAX &&
           (config->sps[0] & 31) == 7 && (config->pps[0] & 31) == 8 &&
           config->chroma_format <= 3 && config->bit_depth_luma >= 8 &&
           config->bit_depth_luma <= 14 && config->bit_depth_chroma >= 8 &&
           config->bit_depth_chroma <= 14;
}

static void write_avc_configuration(CcCaptureWriter *writer,
                                    const CcCaptureVideoConfig *encoder) {
    uint64_t config = begin_box(writer, "avcC");
    uint8_t profile = encoder->sps[1];
    write_u8(writer, 1);
    write_u8(writer, profile);
    write_u8(writer, encoder->sps[2]);
    write_u8(writer, encoder->sps[3]);
    write_u8(writer, 0xff);
    write_u8(writer, 0xe1);
    write_u16(writer, (uint16_t)encoder->sps_size);
    write_bytes(writer, encoder->sps, encoder->sps_size);
    write_u8(writer, 1);
    write_u16(writer, (uint16_t)encoder->pps_size);
    write_bytes(writer, encoder->pps, encoder->pps_size);
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244) {
        write_u8(writer, (uint8_t)(0xfc | encoder->chroma_format));
        write_u8(writer, (uint8_t)(0xf8 | (encoder->bit_depth_luma - 8)));
        write_u8(writer, (uint8_t)(0xf8 | (encoder->bit_depth_chroma - 8)));
        write_u8(writer, 0);
    }
    end_box(writer, config);
}

static void write_jpeg_configuration(CcCaptureWriter *writer) {
    uint64_t box = begin_full_box(writer, "esds", 0);
    write_u8(writer, 3); /* ES_Descriptor, including decoder and SL descriptors. */
    write_u8(writer, 21);
    write_u16(writer, 1);
    write_u8(writer, 0);
    write_u8(writer, 4);
    write_u8(writer, 13);
    write_u8(writer, 0x6c); /* ISO/IEC 10918-1 registered VisualStream object type. */
    write_u8(writer, 0x11);
    write_zeros(writer, 11); /* No fixed decoder buffer or bitrate requirement. */
    write_u8(writer, 6);
    write_u8(writer, 1);
    write_u8(writer, 2); /* MP4 supplies access-unit timing. */
    end_box(writer, box);
}

static void write_video_description(CcCaptureWriter *writer) {
    CcCaptureVideoConfig encoder;
    if (!cc_capture_video_config(writer->encoder, &encoder) ||
        !valid_video_config(&encoder)) {
        writer->io_failed = true;
        fail_writer(writer, "Cannot describe the recorded video format.");
        return;
    }
    uint64_t entry =
        begin_box(writer, encoder.codec == CC_CAPTURE_VIDEO_H264 ? "avc1" : "mp4v");
    write_zeros(writer, 6);
    write_u16(writer, 1);
    write_zeros(writer, 16);
    write_u16(writer, (uint16_t)writer->width);
    write_u16(writer, (uint16_t)writer->height);
    write_u32(writer, 72u << 16);
    write_u32(writer, 72u << 16);
    write_u32(writer, 0);
    write_u16(writer, 1);
    write_zeros(writer, 32);
    write_u16(writer, 24);
    write_u16(writer, UINT16_MAX);

    if (encoder.codec == CC_CAPTURE_VIDEO_H264)
        write_avc_configuration(writer, &encoder);
    else
        write_jpeg_configuration(writer);

    uint64_t colour = begin_box(writer, "colr");
    write_bytes(writer, "nclx", 4);
    write_u16(writer, (uint16_t)encoder.color_primaries);
    write_u16(writer, (uint16_t)encoder.transfer_characteristics);
    write_u16(writer, (uint16_t)encoder.matrix_coefficients);
    write_u8(writer, encoder.full_range ? 0x80 : 0);
    end_box(writer, colour);
    end_box(writer, entry);
}

static void write_audio_description(CcCaptureWriter *writer) {
    if (writer->audio_encoder) {
        uint64_t entry = begin_box(writer, "mp4a");
        write_zeros(writer, 6);
        write_u16(writer, 1);
        write_zeros(writer, 8);
        write_u16(writer, 2);
        write_u16(writer, 16);
        write_zeros(writer, 4);
        write_u32(writer, writer->audio_rate << 16);
        uint64_t config = begin_full_box(writer, "esds", 0);
        write_u8(writer, 3); /* ES_Descriptor, decoder configuration and SL timing. */
        write_u8(writer, 25);
        write_u16(writer, 2);
        write_u8(writer, 0);
        write_u8(writer, 4);
        write_u8(writer, 17);
        write_u8(writer, 0x40); /* MPEG-4 Audio. */
        write_u8(writer, 0x15); /* AudioStream, reserved bit set. */
        write_zeros(writer, 3);
        write_u32(writer, writer->audio_config.bitrate);
        write_u32(writer, writer->audio_config.bitrate);
        write_u8(writer, 5);
        write_u8(writer, sizeof(writer->audio_config.decoder_config));
        write_bytes(writer, writer->audio_config.decoder_config,
                    sizeof(writer->audio_config.decoder_config));
        write_u8(writer, 6);
        write_u8(writer, 1);
        write_u8(writer, 2);
        end_box(writer, config);
        end_box(writer, entry);
        return;
    }
    /* ISO/IEC 23003-5:2020: one MP4 sample is one interleaved PCM frame. */
    uint64_t entry = begin_box(writer, "fpcm");
    write_zeros(writer, 6);
    write_u16(writer, 1);
    write_zeros(writer, 8);
    write_u16(writer, 2);
    write_u16(writer, 32);
    write_zeros(writer, 4);
    write_u32(writer, writer->audio_rate << 16);
    uint64_t config = begin_full_box(writer, "pcmC", 0);
    write_u8(writer, 1); /* Explicit little-endian IEEE-754 binary32. */
    write_u8(writer, 32);
    end_box(writer, config);
    uint64_t channels = begin_full_box(writer, "chnl", 0);
    write_u8(writer, 1);
    write_u8(writer, 2); /* CICP stereo: front left, front right. */
    write_u64(writer, 0);
    end_box(writer, channels);
    end_box(writer, entry);
}

static void write_sample_description(CcCaptureWriter *writer, bool video) {
    uint64_t box = begin_full_box(writer, "stsd", 0);
    write_u32(writer, 1);
    if (video)
        write_video_description(writer);
    else
        write_audio_description(writer);
    end_box(writer, box);
}

static uint32_t duration_runs(const CaptureTrack *track) {
    uint32_t runs = 0;
    for (size_t i = 0; i < track->count; ++i) {
        if (i == 0 || track->samples[i].duration != track->samples[i - 1].duration)
            runs++;
    }
    return runs;
}

static void write_time_to_sample(CcCaptureWriter *writer, bool video) {
    const CaptureTrack *track = video ? &writer->video : &writer->audio;
    uint64_t box = begin_full_box(writer, "stts", 0);
    if (!video && !writer->audio_encoder) {
        write_u32(writer, track->duration ? 1 : 0);
        if (track->duration) {
            write_u32(writer, (uint32_t)track->duration);
            write_u32(writer, 1);
        }
    } else {
        write_u32(writer, duration_runs(track));
        size_t i = 0;
        while (i < track->count) {
            size_t end = i + 1;
            uint32_t duration = track->samples[i].duration;
            while (end < track->count && track->samples[end].duration == duration)
                end++;
            write_u32(writer, (uint32_t)(end - i));
            write_u32(writer, duration);
            i = end;
        }
    }
    end_box(writer, box);
}

static void write_sample_to_chunk(CcCaptureWriter *writer, bool video) {
    const CaptureTrack *track = video ? &writer->video : &writer->audio;
    uint64_t box = begin_full_box(writer, "stsc", 0);
    bool access_units = video || writer->audio_encoder;
    write_u32(writer, access_units ? (track->count ? 1 : 0) : duration_runs(track));
    for (size_t i = 0; i < track->count; ++i) {
        if (access_units && i > 0)
            break;
        if (!access_units && i > 0 &&
            track->samples[i].duration == track->samples[i - 1].duration)
            continue;
        write_u32(writer, (uint32_t)i + 1);
        write_u32(writer, access_units ? 1 : track->samples[i].duration);
        write_u32(writer, 1);
    }
    end_box(writer, box);
}

static void write_sample_sizes(CcCaptureWriter *writer, bool video) {
    const CaptureTrack *track = video ? &writer->video : &writer->audio;
    uint64_t box = begin_full_box(writer, "stsz", 0);
    bool access_units = video || writer->audio_encoder;
    write_u32(writer, access_units ? 0 : CAPTURE_AUDIO_FRAME_BYTES);
    write_u32(writer,
              access_units ? (uint32_t)track->count : (uint32_t)track->duration);
    if (access_units) {
        for (size_t i = 0; i < track->count; ++i)
            write_u32(writer, track->samples[i].size);
    }
    end_box(writer, box);
}

static void write_chunk_offsets(CcCaptureWriter *writer, bool video) {
    const CaptureTrack *track = video ? &writer->video : &writer->audio;
    uint64_t box = begin_full_box(writer, "co64", 0);
    write_u32(writer, (uint32_t)track->count);
    for (size_t i = 0; i < track->count; ++i)
        write_u64(writer, track->samples[i].offset);
    end_box(writer, box);
}

static void write_sync_samples(CcCaptureWriter *writer) {
    const CaptureTrack *track = &writer->video;
    uint32_t count = 0;
    for (size_t i = 0; i < track->count; ++i)
        count += track->samples[i].keyframe ? 1u : 0u;
    if (count == track->count)
        return;
    uint64_t box = begin_full_box(writer, "stss", 0);
    write_u32(writer, count);
    for (size_t i = 0; i < track->count; ++i) {
        if (track->samples[i].keyframe)
            write_u32(writer, (uint32_t)i + 1);
    }
    end_box(writer, box);
}

static void write_audio_roll_groups(CcCaptureWriter *writer) {
    if (!writer->audio_encoder || !writer->audio.count)
        return;
    /* AAC-LC needs the previous access unit for overlap reconstruction.
     * Explicit roll groups distinguish decoder pre-roll from presentation
     * priming, preventing players from discarding the initial delay twice. */
    uint64_t description = begin_full_box(writer, "sgpd", 0x1000000);
    write_bytes(writer, "roll", 4);
    write_u32(writer, 2);
    write_u32(writer, 1);
    write_u16(writer, UINT16_MAX); /* Signed roll_distance = -1 access unit. */
    end_box(writer, description);
    uint64_t mapping = begin_full_box(writer, "sbgp", 0);
    write_bytes(writer, "roll", 4);
    write_u32(writer, 1);
    write_u32(writer, (uint32_t)writer->audio.count);
    write_u32(writer, 1);
    end_box(writer, mapping);
}

static void write_sample_table(CcCaptureWriter *writer, bool video) {
    uint64_t box = begin_box(writer, "stbl");
    write_sample_description(writer, video);
    if (!video)
        write_audio_roll_groups(writer);
    write_time_to_sample(writer, video);
    write_sample_to_chunk(writer, video);
    write_sample_sizes(writer, video);
    write_chunk_offsets(writer, video);
    if (video)
        write_sync_samples(writer);
    end_box(writer, box);
}

static void write_media_information(CcCaptureWriter *writer, bool video) {
    uint64_t container = begin_box(writer, "minf");
    uint64_t header = begin_full_box(writer, video ? "vmhd" : "smhd", video ? 1 : 0);
    write_zeros(writer, video ? 8 : 4);
    end_box(writer, header);
    write_data_information(writer);
    write_sample_table(writer, video);
    end_box(writer, container);
}

static void write_track(CcCaptureWriter *writer, bool video, uint64_t duration) {
    uint64_t track = begin_box(writer, "trak");
    write_track_header(writer, video, duration);
    if (!video && writer->audio_encoder) {
        /* Skip the codec's reported priming frames and expose only real input.
         * Media samples retain complete 1024-frame AAC units for decoding. */
        uint64_t edit = begin_box(writer, "edts");
        uint64_t list = begin_full_box(writer, "elst", 0x1000000);
        write_u32(writer, 1);
        write_u64(writer, duration);
        write_u64(writer, writer->audio_config.priming_frames);
        write_u16(writer, 1);
        write_u16(writer, 0);
        end_box(writer, list);
        end_box(writer, edit);
    }
    uint64_t media = begin_box(writer, "mdia");
    write_media_header(writer, video);
    write_media_information(writer, video);
    end_box(writer, media);
    end_box(writer, track);
}

static uint64_t audio_presented_frames(const CcCaptureWriter *writer) {
    if (!writer->audio_encoder)
        return writer->audio.duration;
    uint64_t available =
        writer->audio.duration > writer->audio_config.priming_frames
            ? writer->audio.duration - writer->audio_config.priming_frames
            : 0;
    return writer->audio_input_frames < available ? writer->audio_input_frames
                                                  : available;
}

static void write_audio_gapless_metadata(CcCaptureWriter *writer) {
    if (!writer->audio_encoder || !writer->audio.count)
        return;
    uint64_t frames = audio_presented_frames(writer);
    uint64_t coded = writer->audio.duration;
    uint32_t priming = writer->audio_config.priming_frames;
    uint32_t padding =
        coded >= frames + priming ? (uint32_t)(coded - frames - priming) : 0;
    char timing[128];
    int length = snprintf(timing, sizeof(timing),
                          " 00000000 %08" PRIX32 " %08" PRIX32 " %016" PRIX64
                          " 00000000 00000000 00000000 00000000"
                          " 00000000 00000000 00000000 00000000",
                          priming, padding, frames);
    if (length < 0 || (size_t)length >= sizeof(timing)) {
        writer->io_failed = true;
        fail_writer(writer, "Cannot describe AAC padding.");
        return;
    }
    /* Audio-file readers also need this standard gapless field because some
     * ignore presentation edit lists. It contains only sample counts. */
    uint64_t user_data = begin_box(writer, "udta");
    uint64_t metadata = begin_full_box(writer, "meta", 0);
    uint64_t handler = begin_full_box(writer, "hdlr", 0);
    write_u32(writer, 0);
    write_bytes(writer, "mdirappl", 8);
    write_zeros(writer, 10);
    end_box(writer, handler);
    uint64_t list = begin_box(writer, "ilst");
    uint64_t field = begin_box(writer, "----");
    uint64_t owner = begin_full_box(writer, "mean", 0);
    write_bytes(writer, "com.apple.iTunes", 16);
    end_box(writer, owner);
    uint64_t name = begin_full_box(writer, "name", 0);
    write_bytes(writer, "iTunSMPB", 8);
    end_box(writer, name);
    uint64_t data = begin_full_box(writer, "data", 1);
    write_u32(writer, 0);
    write_bytes(writer, timing, (size_t)length);
    end_box(writer, data);
    end_box(writer, field);
    end_box(writer, list);
    end_box(writer, metadata);
    end_box(writer, user_data);
}

static bool finalize_recording(CcCaptureWriter *writer) {
    uint64_t video_duration;
    uint64_t audio_duration;
    uint64_t audio_frames = audio_presented_frames(writer);
    if (!movie_duration(writer->video.duration, writer->video_timescale,
                        writer->movie_timescale, &video_duration) ||
        !movie_duration(audio_frames, writer->audio_rate, writer->movie_timescale,
                        &audio_duration))
        return fail_writer(writer, "Recording duration exceeds file format limits.");

    clearerr(writer->file);
    writer->io_failed = false;
    if (!seek_file(writer->file, writer->complete_position))
        return fail_writer(writer, "Cannot finalize completed recording frames.");
    writer->position = writer->complete_position;
    uint64_t movie = begin_box(writer, "moov");
    write_movie_header(writer, video_duration > audio_duration ? video_duration
                                                               : audio_duration);
    if (writer->video.count)
        write_track(writer, true, video_duration);
    write_track(writer, false, audio_duration);
    write_audio_gapless_metadata(writer);
    end_box(writer, movie);
    if (writer->io_failed)
        return false;

    uint64_t end = writer->position;
    if (!seek_file(writer->file, writer->mdat_position + 8))
        return fail_writer(writer, "Cannot finalize recording media size.");
    writer->position = writer->mdat_position + 8;
    write_u64(writer, writer->complete_position - writer->mdat_position);
    if (writer->io_failed || !truncate_file(writer->file, end))
        return fail_writer(writer, "Cannot flush completed recording.");
    return true;
}

bool cc_capture_writer_close(CcCaptureWriter *writer) {
    if (!writer)
        return true;
    bool audio_complete =
        !writer->audio_encoder ||
        cc_capture_audio_finish(writer->audio_encoder, append_audio_packet, writer);
    if (audio_complete && writer->audio_encoder) {
        audio_complete =
            cc_capture_audio_config(writer->audio_encoder, &writer->audio_config);
        if (audio_complete &&
            audio_presented_frames(writer) != writer->audio_input_frames)
            audio_complete =
                fail_writer(writer, "AAC encoder omitted recording samples.");
    }
    if (!audio_complete)
        fail_writer(writer, cc_capture_audio_error(writer->audio_encoder));
    bool okay = finalize_recording(writer) && audio_complete;
    if (fclose(writer->file) != 0)
        okay = false;
    okay = okay && !writer->error;
    free(writer->video.samples);
    free(writer->audio.samples);
    cc_capture_video_close(writer->encoder);
    cc_capture_audio_close(writer->audio_encoder);
    free(writer);
    return okay;
}
