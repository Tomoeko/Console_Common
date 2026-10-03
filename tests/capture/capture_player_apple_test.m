#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void test_player(const char *directory, unsigned rate, size_t input_frames,
                        bool rejected_append, size_t start_frame) {
    assert(start_frame < input_frames);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/capture-web-%u-%zu-%u.mp4", directory,
                          rate, input_frames, (unsigned)rejected_append);
    assert(length > 0 && (size_t)length < sizeof(path));
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    NSArray<AVAssetTrack *> *tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
    assert(tracks.count == 1);
    AVAssetTrack *track = tracks.firstObject;
    CMTime expected = CMTimeMake((int64_t)input_frames, (int32_t)rate);
    assert(CMTimeCompare(track.timeRange.start, kCMTimeZero) == 0);
    assert(CMTimeCompare(track.timeRange.duration, expected) == 0);
    NSError *error = nil;
    AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    assert(reader && !error);
    reader.timeRange = CMTimeRangeMake(
        CMTimeMake((int64_t)start_frame, (int32_t)rate),
        CMTimeMake((int64_t)(input_frames - start_frame), (int32_t)rate));
    NSDictionary *settings = @{
        AVFormatIDKey : @(kAudioFormatLinearPCM),
        AVLinearPCMBitDepthKey : @32,
        AVLinearPCMIsFloatKey : @YES,
        AVLinearPCMIsNonInterleaved : @NO,
        AVSampleRateKey : @(rate),
        AVNumberOfChannelsKey : @2
    };
    AVAssetReaderTrackOutput *output =
        [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
    assert(output && [reader canAddOutput:output]);
    [reader addOutput:output];
    assert([reader startReading]);
    size_t decoded_frames = 0;
    double squared_error = 0;
    double signal_energy = 0;
    CMSampleBufferRef sample;
    while ((sample = [output copyNextSampleBuffer])) {
        CMItemCount count = CMSampleBufferGetNumSamples(sample);
        assert(count > 0 &&
               (uint64_t)count <= input_frames - start_frame - decoded_frames);
        CMTime timestamp = CMSampleBufferGetPresentationTimeStamp(sample);
        assert(
            CMTimeCompare(timestamp, CMTimeMake((int64_t)(start_frame + decoded_frames),
                                                (int32_t)rate)) == 0);
        CMBlockBufferRef data = CMSampleBufferGetDataBuffer(sample);
        size_t bytes = (size_t)count * sizeof(float) * 2;
        assert(data && CMBlockBufferGetDataLength(data) == bytes);
        float *pcm = malloc(bytes);
        assert(pcm && CMBlockBufferCopyDataBytes(data, 0, bytes, pcm) == noErr);
        for (size_t frame = 0; frame < (size_t)count; ++frame) {
            size_t index = start_frame + decoded_frames + frame;
            double envelope = index < 64 ? (double)index / 64 : 1;
            const double amplitudes[2] = {0.5, 0.35};
            const unsigned frequencies[2] = {997, 1777};
            for (size_t channel = 0; channel < 2; ++channel) {
                double source = envelope * amplitudes[channel] *
                                sin((double)index * frequencies[channel] *
                                    6.283185307179586 / rate);
                double difference = pcm[frame * 2 + channel] - source;
                signal_energy += source * source;
                squared_error += difference * difference;
            }
        }
        free(pcm);
        decoded_frames += (size_t)count;
        CFRelease(sample);
    }
    assert(reader.status == AVAssetReaderStatusCompleted && !reader.error);
    assert(decoded_frames == input_frames - start_frame);
    if (input_frames > rate / 2)
        assert(signal_energy > 1 && squared_error / signal_energy < 0.001);
}

int main(int argc, char **argv) {
    assert(argc == 2 && argv[1][0]);
    @autoreleasepool {
        test_player(argv[1], 48000, 48013, false, 0);
        test_player(argv[1], 32000, 32037, false, 0);
        test_player(argv[1], 48000, 1, false, 0);
        test_player(argv[1], 48000, 1024, false, 0);
        test_player(argv[1], 48000, 5389, true, 0);
        test_player(argv[1], 48000, 48013, false, 8001);
    }
    return 0;
}
