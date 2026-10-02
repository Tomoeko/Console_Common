#ifndef CC_CAPTURE_VIDEO_H
#define CC_CAPTURE_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CcCaptureVideo CcCaptureVideo;

typedef enum { CC_CAPTURE_VIDEO_H264, CC_CAPTURE_VIDEO_JPEG } CcCaptureVideoCodec;

typedef struct {
    CcCaptureVideoCodec codec;
    const uint8_t *sps;
    const uint8_t *pps;
    size_t sps_size;
    size_t pps_size;
    unsigned chroma_format;
    unsigned bit_depth_luma;
    unsigned bit_depth_chroma;
    unsigned color_primaries;
    unsigned transfer_characteristics;
    unsigned matrix_coefficients;
    bool full_range;
} CcCaptureVideoConfig;

/* Encoder state owns reusable storage. Calls are serialized on the writer
 * thread. Samples are complete MP4 payloads, borrowed until the next encode:
 * H.264 uses four-byte AVC NAL lengths; JPEG includes its complete image.
 * Configuration is available immediately; H.264 SPS/PPS arrive with the first
 * encoded frame and remain borrowed until close. RGB input is not resized. */
CcCaptureVideo *cc_capture_video_open(unsigned width, unsigned height,
                                      uint32_t video_timescale);
bool cc_capture_video_encode(CcCaptureVideo *video, const uint8_t *rgba,
                             size_t row_stride, uint32_t duration_ticks,
                             const uint8_t **sample, size_t *size, bool *keyframe);
bool cc_capture_video_config(const CcCaptureVideo *video, CcCaptureVideoConfig *config);
const char *cc_capture_video_error(const CcCaptureVideo *video);
void cc_capture_video_close(CcCaptureVideo *video);

#endif
