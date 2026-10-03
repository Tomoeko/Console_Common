#ifndef CONSOLE_COMMON_CAPTURE_RECORDING_H
#define CONSOLE_COMMON_CAPTURE_RECORDING_H

#include "console_common/platform/platform.h"
#include "console_common/capture/capture_writer.h"

typedef struct CcRecording CcRecording;

/* Begin establishes the maximum queued frame count. Read returns at most its
 * supplied capacity; after the producer stops, queued reads terminate at zero.
 * A failed begin releases its own partial state; end follows a successful begin.
 */
typedef struct {
    void *context;
    bool (*begin)(void *context, size_t capacity_frames);
    size_t (*read)(void *context, float *stereo, size_t capacity_frames);
    bool (*failed)(void *context);
    void (*end)(void *context);
} CcRecordingAudioSource;

typedef struct {
    CcPlatform *platform;
    CcRecordingAudioSource audio;
    unsigned sample_rate;
    unsigned video_rate;
    const char *filename_prefix;
    bool half_size;
    CcCaptureAudioMode audio_mode;
} CcRecordingOptions;

/* Options are copied; platform and audio context remain borrowed until close.
 * Supply all audio callbacks for audible recording, or none for silence.
 * Sample rate is 8000..65535 Hz; video rate is 50 or 60 Hz. The optional filename
 * prefix is 1..64 ASCII letters, digits, hyphens or underscores; NULL uses
 * "Recording". The default destination is the current user's Movies folder.
 * The owner stops audio rendering before opening, suspending, or closing.
 * Capture retains the initial backing-pixel size across window changes. Half
 * size averages 2x2 source pixels and preserves odd edges in rounded-up dimensions.
 */
CcRecording *cc_recording_open(const CcRecordingOptions *options);
CcRecording *cc_recording_open_path(const CcRecordingOptions *options,
                                    const char *path);
const char *cc_recording_path(const CcRecording *recording);
const char *cc_recording_error(const CcRecording *recording);
bool cc_recording_frame(CcRecording *recording, double monotonic_seconds);
bool cc_recording_pump(CcRecording *recording, double monotonic_seconds);
bool cc_recording_audio_start(CcRecording *recording, double monotonic_seconds);
bool cc_recording_audio_stop(CcRecording *recording, double monotonic_seconds);
bool cc_recording_close(CcRecording *recording, double monotonic_seconds);

#endif
