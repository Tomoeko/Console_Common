#include "capture_video.h"
#include "capture_jpeg.h"

#include <stdlib.h>

struct CcCaptureVideo {
    CcCaptureJpeg *jpeg;
    bool failed;
};

CcCaptureVideo *cc_capture_video_open(unsigned width, unsigned height,
                                      uint32_t video_timescale) {
    if (!video_timescale)
        return NULL;
    CcCaptureVideo *video = calloc(1, sizeof(*video));
    if (!video)
        return NULL;
    video->jpeg = cc_capture_jpeg_open(width, height, 97);
    if (!video->jpeg) {
        cc_capture_video_close(video);
        return NULL;
    }
    return video;
}

bool cc_capture_video_encode(CcCaptureVideo *video, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks,
                             const uint8_t **sample, size_t *size, bool *keyframe) {
    if (!video)
        return false;
    if (video->failed || !duration_ticks || !keyframe ||
        !cc_capture_jpeg_encode(video->jpeg, rgba, row_stride, sample, size)) {
        video->failed = true;
        return false;
    }
    *keyframe = true;
    return true;
}

bool cc_capture_video_config(const CcCaptureVideo *video,
                             CcCaptureVideoConfig *config) {
    if (!video || !config)
        return false;
    *config = (CcCaptureVideoConfig){.codec = CC_CAPTURE_VIDEO_JPEG,
                                     .chroma_format = 3,
                                     .bit_depth_luma = 8,
                                     .bit_depth_chroma = 8,
                                     .color_primaries = 1,
                                     .transfer_characteristics = 13,
                                     .matrix_coefficients = 6,
                                     .full_range = true};
    return true;
}

const char *cc_capture_video_error(const CcCaptureVideo *video) {
    return video && video->failed ? "JPEG video compression failed." : "";
}

void cc_capture_video_close(CcCaptureVideo *video) {
    if (!video)
        return;
    cc_capture_jpeg_close(video->jpeg);
    free(video);
}
