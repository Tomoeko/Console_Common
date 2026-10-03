#include "capture/capture_video.h"
#include "capture/capture_jpeg.h"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CAPTURE_PARAMETER_BYTES = 1024,
    CAPTURE_VIDEO_FRAMES_PER_SECOND = 60,
    CAPTURE_KEYFRAME_INTERVAL = 120
};

bool cc_capture_video_web_available(void) {
    return true;
}

struct CcCaptureVideo {
    VTCompressionSessionRef session;
    CVPixelBufferRef pixels;
    CcCaptureJpeg *jpeg;
    pthread_mutex_t mutex;
    bool mutex_initialized;
    bool pending;
    bool completed;
    bool failed;
    bool keyframe;
    uint8_t *sample;
    size_t sample_capacity;
    size_t sample_size;
    uint8_t sps[CAPTURE_PARAMETER_BYTES];
    uint8_t pps[CAPTURE_PARAMETER_BYTES];
    CcCaptureVideoConfig config;
    unsigned width;
    unsigned height;
    int32_t timescale;
    int64_t presentation_ticks;
    char error[128];
};

static bool fail_video(CcCaptureVideo *video, const char *message, OSStatus status) {
    if (!video->error[0]) {
        if (status == noErr)
            snprintf(video->error, sizeof(video->error), "%s", message);
        else
            snprintf(video->error, sizeof(video->error), "%s (status %d)", message,
                     (int)status);
    }
    return false;
}

static bool set_number(VTCompressionSessionRef session, CFStringRef property,
                       CFNumberType type, const void *value, bool optional) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, type, value);
    if (!number)
        return false;
    OSStatus status = VTSessionSetProperty(session, property, number);
    CFRelease(number);
    return status == noErr || (optional && status == kVTPropertyNotSupportedErr);
}

static bool configure_session(CcCaptureVideo *video) {
    int32_t bitrate = (int32_t)((uint64_t)video->width * video->height * 36 / 5);
    if (bitrate < 2000000)
        bitrate = 2000000;
    int32_t fps = CAPTURE_VIDEO_FRAMES_PER_SECOND;
    int32_t keyframe_interval = CAPTURE_KEYFRAME_INTERVAL;
    double quality = 0.95;
    VTCompressionSessionRef session = video->session;
    return VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel,
                                kVTProfileLevel_H264_High_AutoLevel) == noErr &&
           VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering,
                                kCFBooleanFalse) == noErr &&
           VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime,
                                kCFBooleanTrue) == noErr &&
           set_number(session, kVTCompressionPropertyKey_AverageBitRate,
                      kCFNumberSInt32Type, &bitrate, false) &&
           set_number(session, kVTCompressionPropertyKey_ExpectedFrameRate,
                      kCFNumberSInt32Type, &fps, true) &&
           set_number(session, kVTCompressionPropertyKey_MaxKeyFrameInterval,
                      kCFNumberSInt32Type, &keyframe_interval, false) &&
           set_number(session, kVTCompressionPropertyKey_Quality, kCFNumberDoubleType,
                      &quality, true) &&
           VTSessionSetProperty(session, kVTCompressionPropertyKey_ColorPrimaries,
                                kCVImageBufferColorPrimaries_ITU_R_709_2) == noErr &&
           VTSessionSetProperty(session, kVTCompressionPropertyKey_TransferFunction,
                                kCVImageBufferTransferFunction_sRGB) == noErr &&
           VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix,
                                kCVImageBufferYCbCrMatrix_ITU_R_709_2) == noErr;
}

static bool capture_parameters(CcCaptureVideo *video, CMFormatDescriptionRef format) {
    if (!format || CMFormatDescriptionGetMediaSubType(format) != kCMVideoCodecType_H264)
        return fail_video(video, "Native H.264 format is missing", noErr);
    const uint8_t *sps;
    const uint8_t *pps;
    size_t sps_size;
    size_t pps_size;
    size_t parameter_count;
    int nal_length_bytes;
    if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            format, 0, &sps, &sps_size, &parameter_count, &nal_length_bytes) != noErr ||
        parameter_count != 2 || nal_length_bytes != 4 || !sps || sps_size < 4 ||
        sps_size > sizeof(video->sps) || (sps[0] & 31) != 7 ||
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 1, &pps, &pps_size,
                                                           NULL, NULL) != noErr ||
        !pps || !pps_size || pps_size > sizeof(video->pps) || (pps[0] & 31) != 8)
        return fail_video(video, "Native H.264 parameter sets are invalid", noErr);
    if (video->config.sps_size) {
        bool same =
            sps_size == video->config.sps_size && pps_size == video->config.pps_size &&
            !memcmp(video->sps, sps, sps_size) && !memcmp(video->pps, pps, pps_size);
        return same || fail_video(video, "Native H.264 parameter sets changed", noErr);
    }
    memcpy(video->sps, sps, sps_size);
    memcpy(video->pps, pps, pps_size);
    video->config.sps = video->sps;
    video->config.pps = video->pps;
    video->config.sps_size = sps_size;
    video->config.pps_size = pps_size;
    CFTypeRef range = CMFormatDescriptionGetExtension(
        format, kCMFormatDescriptionExtension_FullRangeVideo);
    /* CoreMedia defines an absent range flag as video-range for compressed YCbCr. */
    video->config.full_range = range && CFEqual(range, kCFBooleanTrue);
    return true;
}

static void encoded_frame(void *context, void *source_context, OSStatus status,
                          VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
    CcCaptureVideo *video = context;
    pthread_mutex_lock(&video->mutex);
    if (status != noErr)
        fail_video(video, "Native H.264 output callback failed", status);
    else if (flags & kVTEncodeInfo_FrameDropped)
        fail_video(video, "Native H.264 encoder dropped a frame", noErr);
    bool okay = source_context == video && video->pending && status == noErr &&
                !(flags & kVTEncodeInfo_FrameDropped) && sample &&
                CMSampleBufferDataIsReady(sample) &&
                CMSampleBufferGetNumSamples(sample) == 1;
    CMBlockBufferRef data = okay ? CMSampleBufferGetDataBuffer(sample) : NULL;
    size_t size = data ? CMBlockBufferGetDataLength(data) : 0;
    if (size > video->sample_capacity)
        fail_video(video, "Native H.264 sample exceeds capture storage", noErr);
    okay =
        okay && size > 0 && size <= video->sample_capacity &&
        capture_parameters(video, CMSampleBufferGetFormatDescription(sample)) &&
        CMBlockBufferCopyDataBytes(data, 0, size, video->sample) == kCMBlockBufferNoErr;
    if (okay) {
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        CFDictionaryRef attachment = attachments && CFArrayGetCount(attachments) == 1
                                         ? CFArrayGetValueAtIndex(attachments, 0)
                                         : NULL;
        CFTypeRef not_sync =
            attachment
                ? CFDictionaryGetValue(attachment, kCMSampleAttachmentKey_NotSync)
                : NULL;
        video->keyframe = !not_sync || !CFEqual(not_sync, kCFBooleanTrue);
        video->sample_size = size;
    }
    video->failed = video->failed || !okay;
    if (!okay)
        fail_video(video, "Native H.264 output sample is invalid", noErr);
    video->pending = false;
    video->completed = true;
    pthread_mutex_unlock(&video->mutex);
}

static bool create_session(CcCaptureVideo *video) {
    int32_t width = (int32_t)video->width;
    int32_t height = (int32_t)video->height;
    int32_t pixel_format = (int32_t)kCVPixelFormatType_32BGRA;
    CFNumberRef width_number = CFNumberCreate(NULL, kCFNumberSInt32Type, &width);
    CFNumberRef height_number = CFNumberCreate(NULL, kCFNumberSInt32Type, &height);
    CFNumberRef format_number =
        CFNumberCreate(NULL, kCFNumberSInt32Type, &pixel_format);
    CFDictionaryRef surface =
        CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef attributes = NULL;
    CFDictionaryRef specification = NULL;
    bool okay = false;
    if (!width_number || !height_number || !format_number || !surface)
        goto release_attributes;
    const void *keys[] = {kCVPixelBufferWidthKey, kCVPixelBufferHeightKey,
                          kCVPixelBufferPixelFormatTypeKey,
                          kCVPixelBufferIOSurfacePropertiesKey};
    const void *values[] = {width_number, height_number, format_number, surface};
    attributes =
        CFDictionaryCreate(NULL, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    const void *encoder_keys[] = {
        kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
    const void *encoder_values[] = {kCFBooleanTrue};
    specification = CFDictionaryCreate(NULL, encoder_keys, encoder_values, 1,
                                       &kCFTypeDictionaryKeyCallBacks,
                                       &kCFTypeDictionaryValueCallBacks);
    if (!attributes || !specification ||
        VTCompressionSessionCreate(NULL, width, height, kCMVideoCodecType_H264,
                                   specification, attributes, NULL, encoded_frame,
                                   video, &video->session) != noErr ||
        !configure_session(video) ||
        VTCompressionSessionPrepareToEncodeFrames(video->session) != noErr)
        goto release_attributes;
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(video->session);
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &video->pixels) !=
                     kCVReturnSuccess)
        goto release_attributes;
    CVBufferSetAttachment(video->pixels, kCVImageBufferColorPrimariesKey,
                          kCVImageBufferColorPrimaries_ITU_R_709_2,
                          kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(video->pixels, kCVImageBufferTransferFunctionKey,
                          kCVImageBufferTransferFunction_sRGB,
                          kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(video->pixels, kCVImageBufferYCbCrMatrixKey,
                          kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                          kCVAttachmentMode_ShouldPropagate);
    okay = true;

release_attributes:
    if (specification)
        CFRelease(specification);
    if (attributes)
        CFRelease(attributes);
    if (surface)
        CFRelease(surface);
    if (format_number)
        CFRelease(format_number);
    if (height_number)
        CFRelease(height_number);
    if (width_number)
        CFRelease(width_number);
    return okay;
}

static void release_session(CcCaptureVideo *video) {
    if (video->session) {
        VTCompressionSessionCompleteFrames(video->session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(video->session);
        CFRelease(video->session);
        video->session = NULL;
    }
    if (video->pixels) {
        CVPixelBufferRelease(video->pixels);
        video->pixels = NULL;
    }
}

static bool select_jpeg(CcCaptureVideo *video) {
    release_session(video);
    video->jpeg = cc_capture_jpeg_open(video->width, video->height, 97);
    if (!video->jpeg)
        return false;
    video->config = (CcCaptureVideoConfig){.codec = CC_CAPTURE_VIDEO_JPEG,
                                           .chroma_format = 3,
                                           .bit_depth_luma = 8,
                                           .bit_depth_chroma = 8,
                                           .color_primaries = 1,
                                           .transfer_characteristics = 13,
                                           .matrix_coefficients = 6,
                                           .full_range = true};
    return true;
}

CcCaptureVideo *cc_capture_video_open(unsigned width, unsigned height,
                                      uint32_t video_timescale) {
    if (!width || !height || width > 4096 || height > 4096 || !video_timescale ||
        video_timescale > INT32_MAX || (size_t)width * height > (SIZE_MAX - 65536) / 4)
        return NULL;
    CcCaptureVideo *video = calloc(1, sizeof(*video));
    if (!video)
        return NULL;
    video->width = width;
    video->height = height;
    video->timescale = (int32_t)video_timescale;
    video->config = (CcCaptureVideoConfig){.codec = CC_CAPTURE_VIDEO_H264,
                                           .chroma_format = 1,
                                           .bit_depth_luma = 8,
                                           .bit_depth_chroma = 8,
                                           .color_primaries = 1,
                                           .transfer_characteristics = 13,
                                           .matrix_coefficients = 1};
    if (pthread_mutex_init(&video->mutex, NULL) != 0)
        goto release_video;
    video->mutex_initialized = true;
    /* 4:2:0 H.264 cannot represent an odd visible raster with its crop units. */
    if (width < 16 || height < 16 || (width & 1) || (height & 1) ||
        !create_session(video)) {
        if (!select_jpeg(video))
            goto release_video;
        return video;
    }
    video->sample_capacity = (size_t)width * height * 4 + 65536;
    video->sample = malloc(video->sample_capacity);
    if (!video->sample)
        goto release_video;
    return video;

release_video:
    cc_capture_video_close(video);
    return NULL;
}

static bool copy_pixels(CcCaptureVideo *video, const uint8_t *rgba, size_t stride) {
    if (CVPixelBufferLockBaseAddress(video->pixels, 0) != kCVReturnSuccess)
        return false;
    uint8_t *destination = CVPixelBufferGetBaseAddress(video->pixels);
    size_t destination_stride = CVPixelBufferGetBytesPerRow(video->pixels);
    bool okay = destination && destination_stride >= (size_t)video->width * 4 &&
                CVPixelBufferGetWidth(video->pixels) == video->width &&
                CVPixelBufferGetHeight(video->pixels) == video->height;
    if (okay) {
        for (unsigned y = 0; y < video->height; ++y) {
            const uint8_t *input = rgba + (size_t)y * stride;
            uint8_t *output = destination + (size_t)y * destination_stride;
            for (unsigned x = 0; x < video->width; ++x) {
                size_t pixel = (size_t)x * 4;
                output[pixel] = input[pixel + 2];
                output[pixel + 1] = input[pixel + 1];
                output[pixel + 2] = input[pixel];
                output[pixel + 3] = 255;
            }
        }
    }
    return CVPixelBufferUnlockBaseAddress(video->pixels, 0) == kCVReturnSuccess && okay;
}

bool cc_capture_video_encode(CcCaptureVideo *video, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks,
                             const uint8_t **sample, size_t *size, bool *keyframe) {
    if (!video || !rgba || !sample || !size || !keyframe || !duration_ticks ||
        row_stride < (size_t)video->width * 4 ||
        row_stride > SIZE_MAX / video->height ||
        duration_ticks > (uint64_t)(INT64_MAX - video->presentation_ticks))
        return false;
    if (video->jpeg) {
        if (!cc_capture_jpeg_encode(video->jpeg, rgba, row_stride, sample, size))
            return fail_video(video, "JPEG video compression failed", noErr);
        video->presentation_ticks += duration_ticks;
        *keyframe = true;
        return true;
    }
    if (!copy_pixels(video, rgba, row_stride))
        return fail_video(video, "Native H.264 pixel buffer copy failed", noErr);
    pthread_mutex_lock(&video->mutex);
    bool okay = !video->failed && !video->pending;
    video->pending = okay;
    video->completed = false;
    video->sample_size = 0;
    pthread_mutex_unlock(&video->mutex);
    if (!okay)
        return false;
    CMTime presentation = CMTimeMake(video->presentation_ticks, video->timescale);
    CMTime duration = CMTimeMake(duration_ticks, video->timescale);
    VTEncodeInfoFlags flags = 0;
    OSStatus submit_status = VTCompressionSessionEncodeFrame(
        video->session, video->pixels, presentation, duration, NULL, video, &flags);
    OSStatus complete_status =
        submit_status == noErr
            ? VTCompressionSessionCompleteFrames(video->session, presentation)
            : submit_status;
    pthread_mutex_lock(&video->mutex);
    if (submit_status != noErr)
        fail_video(video, "Native H.264 frame submission failed", submit_status);
    else if (complete_status != noErr)
        fail_video(video, "Native H.264 frame completion failed", complete_status);
    okay = submit_status == noErr && complete_status == noErr &&
           !(flags & kVTEncodeInfo_FrameDropped) && video->completed &&
           !video->failed && video->sample_size > 0;
    if (okay) {
        *sample = video->sample;
        *size = video->sample_size;
        *keyframe = video->keyframe;
        video->presentation_ticks += duration_ticks;
    } else {
        video->failed = true;
        fail_video(video, "Native H.264 frame callback did not complete", noErr);
    }
    pthread_mutex_unlock(&video->mutex);
    return okay;
}

bool cc_capture_video_config(const CcCaptureVideo *video,
                             CcCaptureVideoConfig *config) {
    if (!video || !config)
        return false;
    /* encode completes every callback before exposing this borrowed configuration. */
    *config = video->config;
    return true;
}

const char *cc_capture_video_error(const CcCaptureVideo *video) {
    return video ? video->error : "";
}

void cc_capture_video_close(CcCaptureVideo *video) {
    if (!video)
        return;
    release_session(video);
    cc_capture_jpeg_close(video->jpeg);
    if (video->mutex_initialized)
        pthread_mutex_destroy(&video->mutex);
    free(video->sample);
    free(video);
}
