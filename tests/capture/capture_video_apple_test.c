#include "capture/capture_video.h"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TEST_WIDTH = 320, TEST_HEIGHT = 240, TEST_STRIDE = TEST_WIDTH * 4 + 13 };

typedef struct {
    pthread_mutex_t mutex;
    uint8_t bgra[TEST_WIDTH * TEST_HEIGHT * 4];
    unsigned callbacks;
    bool failed;
} DecodedFrame;

static void decoded_frame(void *context, void *source_context, OSStatus status,
                          VTDecodeInfoFlags flags, CVImageBufferRef image,
                          CMTime presentation, CMTime duration) {
    (void)presentation;
    (void)duration;
    DecodedFrame *decoded = context;
    pthread_mutex_lock(&decoded->mutex);
    bool okay = source_context == decoded && status == noErr && image &&
                !(flags & kVTDecodeInfo_FrameDropped) &&
                CVPixelBufferGetWidth(image) == TEST_WIDTH &&
                CVPixelBufferGetHeight(image) == TEST_HEIGHT &&
                CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) ==
                    kCVReturnSuccess;
    if (okay) {
        const uint8_t *pixels = CVPixelBufferGetBaseAddress(image);
        size_t stride = CVPixelBufferGetBytesPerRow(image);
        okay = pixels && stride >= TEST_WIDTH * 4;
        if (okay) {
            for (unsigned y = 0; y < TEST_HEIGHT; ++y)
                memcpy(decoded->bgra + (size_t)y * TEST_WIDTH * 4,
                       pixels + (size_t)y * stride, TEST_WIDTH * 4);
        }
        okay = CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly) ==
                   kCVReturnSuccess &&
               okay;
    }
    decoded->failed = decoded->failed || !okay;
    decoded->callbacks++;
    pthread_mutex_unlock(&decoded->mutex);
}

static VTDecompressionSessionRef create_decoder(CMFormatDescriptionRef format,
                                                DecodedFrame *decoded) {
    int32_t pixel_format = (int32_t)kCVPixelFormatType_32BGRA;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &pixel_format);
    assert(number);
    const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
    const void *values[] = {number};
    CFDictionaryRef attributes =
        CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    assert(attributes);
    VTDecompressionOutputCallbackRecord callback = {decoded_frame, decoded};
    VTDecompressionSessionRef session;
    assert(VTDecompressionSessionCreate(NULL, format, NULL, attributes, &callback,
                                        &session) == noErr);
    CFRelease(attributes);
    CFRelease(number);
    return session;
}

static void fill_pixels(uint8_t *rgba, unsigned frame) {
    static const uint8_t colors[4][3] = {
        {230, 20, 40}, {20, 200, 50}, {30, 40, 210}, {180, 190, 200}};
    memset(rgba, 0xa5, TEST_STRIDE * TEST_HEIGHT);
    for (unsigned y = 0; y < TEST_HEIGHT; ++y) {
        for (unsigned x = 0; x < TEST_WIDTH; ++x) {
            unsigned quadrant =
                (y >= TEST_HEIGHT / 2 ? 2u : 0u) + (x >= TEST_WIDTH / 2 ? 1u : 0u);
            uint8_t *pixel = rgba + (size_t)y * TEST_STRIDE + (size_t)x * 4;
            for (unsigned c = 0; c < 3; ++c)
                pixel[c] = (uint8_t)(colors[quadrant][c] + frame * 3);
            pixel[3] = (uint8_t)(x + y);
        }
    }
}

static void check_sample(const uint8_t *sample, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        assert(size - offset >= 4);
        uint32_t length = (uint32_t)sample[offset] << 24 |
                          (uint32_t)sample[offset + 1] << 16 |
                          (uint32_t)sample[offset + 2] << 8 | sample[offset + 3];
        offset += 4;
        assert(length && length <= size - offset);
        assert((sample[offset] & 31) != 0 && (sample[offset] & 31) < 24);
        offset += length;
    }
    assert(offset == size);
}

static void check_decoded(const DecodedFrame *decoded, const uint8_t *rgba) {
    unsigned total_difference = 0;
    for (unsigned y = 0; y < TEST_HEIGHT; ++y) {
        for (unsigned x = 0; x < TEST_WIDTH; ++x) {
            const uint8_t *source = rgba + (size_t)y * TEST_STRIDE + (size_t)x * 4;
            const uint8_t *pixel = decoded->bgra + ((size_t)y * TEST_WIDTH + x) * 4;
            for (unsigned c = 0; c < 3; ++c) {
                unsigned component = c == 0 ? 2 : c == 2 ? 0 : 1;
                int difference = abs((int)source[c] - (int)pixel[component]);
                total_difference += (unsigned)difference;
                if ((x == TEST_WIDTH / 4 || x == TEST_WIDTH * 3 / 4) &&
                    (y == TEST_HEIGHT / 4 || y == TEST_HEIGHT * 3 / 4))
                    assert(difference <= 8);
            }
        }
    }
    assert(total_difference < TEST_WIDTH * TEST_HEIGHT * 3 * 6);
}

static void test_jpeg_fallback(void) {
    const unsigned dimensions[][2] = {{1, 1}, {19, 17}, {17, 19}};
    for (unsigned i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        unsigned width = dimensions[i][0];
        unsigned height = dimensions[i][1];
        CcCaptureVideo *video = cc_capture_video_open(width, height, 60000);
        assert(video);
        CcCaptureVideoConfig config;
        assert(cc_capture_video_config(video, &config));
        assert(config.codec == CC_CAPTURE_VIDEO_JPEG && !config.sps && !config.pps &&
               config.chroma_format == 3 && config.full_range &&
               config.matrix_coefficients == 6);
        uint8_t rgba[19 * 19 * 4];
        memset(rgba, 80, sizeof(rgba));
        const uint8_t *sample;
        size_t size;
        bool keyframe = false;
        assert(cc_capture_video_encode(video, rgba, (size_t)width * 4, 1000, &sample,
                                       &size, &keyframe));
        assert(size > 4 && keyframe && sample[0] == 0xff && sample[1] == 0xd8 &&
               sample[size - 2] == 0xff && sample[size - 1] == 0xd9);
        cc_capture_video_close(video);
    }
}

int main(void) {
    assert(!cc_capture_video_open(0, 240, 60000));
    assert(!cc_capture_video_open(320, 0, 60000));
    assert(!cc_capture_video_open(4097, 240, 60000));
    assert(!cc_capture_video_open(320, 240, 0));
    assert(!cc_capture_video_open(320, 240, UINT32_MAX));
    assert(!cc_capture_video_config(NULL, NULL));
    cc_capture_video_close(NULL);
    test_jpeg_fallback();
    CcCaptureVideo *video = cc_capture_video_open(TEST_WIDTH, TEST_HEIGHT, 60000);
    assert(video);
    CcCaptureVideoConfig config;
    assert(cc_capture_video_config(video, &config));
    if (config.codec != CC_CAPTURE_VIDEO_H264) {
        cc_capture_video_close(video);
        fputs("Native H.264 encoder unavailable; system codec test skipped.\n", stderr);
        return 77;
    }
    assert(!config.sps && !config.pps && config.chroma_format == 1 &&
           config.bit_depth_luma == 8 && config.bit_depth_chroma == 8);
    uint8_t *rgba = malloc(TEST_STRIDE * TEST_HEIGHT);
    assert(rgba);
    const uint8_t *sample;
    size_t size;
    bool keyframe;
    assert(!cc_capture_video_encode(video, rgba, TEST_STRIDE, 0, &sample, &size,
                                    &keyframe));
    assert(!cc_capture_video_encode(video, rgba, TEST_WIDTH * 4 - 1, 1000, &sample,
                                    &size, &keyframe));
    assert(!cc_capture_video_encode(video, rgba, SIZE_MAX, 1000, &sample, &size,
                                    &keyframe));
    assert(!cc_capture_video_encode(video, NULL, TEST_STRIDE, 1000, &sample, &size,
                                    &keyframe));
    assert(!cc_capture_video_encode(video, rgba, TEST_STRIDE, 1000, NULL, &size,
                                    &keyframe));
    DecodedFrame decoded = {0};
    assert(pthread_mutex_init(&decoded.mutex, NULL) == 0);
    CMFormatDescriptionRef format = NULL;
    VTDecompressionSessionRef decoder = NULL;
    unsigned inter_frames = 0;
    unsigned key_frames = 0;
    int64_t presentation_ticks = 0;
    for (unsigned frame = 0; frame < 246; ++frame) {
        uint32_t duration_ticks = 1000 + frame % 6;
        fill_pixels(rgba, frame % 6);
        assert(cc_capture_video_encode(video, rgba, TEST_STRIDE, duration_ticks,
                                       &sample, &size, &keyframe));
        assert(!cc_capture_video_error(video)[0]);
        key_frames += keyframe;
        check_sample(sample, size);
        assert(cc_capture_video_config(video, &config));
        assert(config.sps_size >= 4 && config.pps_size && config.sps[1] == 100 &&
               config.color_primaries == 1 && config.transfer_characteristics == 13 &&
               config.matrix_coefficients == 1);
        if (!frame) {
            assert(keyframe);
            const uint8_t *parameters[] = {config.sps, config.pps};
            const size_t sizes[] = {config.sps_size, config.pps_size};
            assert(CMVideoFormatDescriptionCreateFromH264ParameterSets(
                       NULL, 2, parameters, sizes, 4, &format) == noErr);
            decoder = create_decoder(format, &decoded);
        } else {
            inter_frames += !keyframe;
        }
        CMBlockBufferRef data;
        assert(CMBlockBufferCreateWithMemoryBlock(NULL, (void *)sample, size,
                                                  kCFAllocatorNull, NULL, 0, size, 0,
                                                  &data) == kCMBlockBufferNoErr);
        CMSampleTimingInfo timing = {CMTimeMake(duration_ticks, 60000),
                                     CMTimeMake(presentation_ticks, 60000),
                                     kCMTimeInvalid};
        presentation_ticks += duration_ticks;
        CMSampleBufferRef compressed;
        assert(CMSampleBufferCreateReady(NULL, data, format, 1, 1, &timing, 1, &size,
                                         &compressed) == noErr);
        VTDecodeInfoFlags flags;
        assert(VTDecompressionSessionDecodeFrame(decoder, compressed, 0, &decoded,
                                                 &flags) == noErr);
        assert(VTDecompressionSessionWaitForAsynchronousFrames(decoder) == noErr);
        pthread_mutex_lock(&decoded.mutex);
        assert(!decoded.failed && decoded.callbacks == frame + 1);
        check_decoded(&decoded, rgba);
        pthread_mutex_unlock(&decoded.mutex);
        CFRelease(compressed);
        CFRelease(data);
    }
    assert(inter_frames && key_frames >= 3);
    VTDecompressionSessionInvalidate(decoder);
    CFRelease(decoder);
    CFRelease(format);
    pthread_mutex_destroy(&decoded.mutex);
    cc_capture_video_close(video);
    free(rgba);
    return 0;
}
