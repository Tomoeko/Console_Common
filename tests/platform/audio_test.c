#define _POSIX_C_SOURCE 200809L

#include "console_common/platform/audio.h"
#include "console_common/support/host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void require(bool condition, int line, const char *expression) {
    if (!condition) {
        fprintf(stderr, "audio_test.c:%d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define REQUIRE(expression) require((expression), __LINE__, #expression)

int main(void) {
    const CcAudioDescription description = {48000, 2, 256, 8};
    char error[256];
    CcAudio *audio = cc_audio_create(&description, error, sizeof(error));
    if (!audio) {
        printf("Native audio device unavailable for 48000Hz stereo: %s\n", error);
        return 77;
    }
    CcAudioDescription unsupported = description;
    unsupported.channels = 3;
    REQUIRE(!cc_audio_create(&unsupported, error, sizeof(error)));
    unsupported = description;
    unsupported.sample_rate = 44100;
    REQUIRE(!cc_audio_create(&unsupported, error, sizeof(error)));
    float samples[512] = {0};
    REQUIRE(cc_audio_writable_buffers(audio) == 8);
    REQUIRE(cc_audio_completed_frames(audio) == 0);
    REQUIRE(!cc_audio_start(audio, error, sizeof(error)));
    REQUIRE(cc_audio_write(audio, NULL, 512, error, sizeof(error)) == CC_AUDIO_INVALID);
    REQUIRE(cc_audio_write(audio, samples, 511, error, sizeof(error)) ==
            CC_AUDIO_INVALID);
    samples[511] = NAN;
    REQUIRE(cc_audio_write(audio, samples, 512, error, sizeof(error)) ==
            CC_AUDIO_INVALID);
    samples[511] = 0;
    for (unsigned buffer = 0; buffer < 8; ++buffer)
        REQUIRE(cc_audio_write(audio, samples, 512, error, sizeof(error)) ==
                CC_AUDIO_QUEUED);
    REQUIRE(cc_audio_writable_buffers(audio) == 0);
    REQUIRE(cc_audio_write(audio, samples, 512, error, sizeof(error)) == CC_AUDIO_FULL);
    REQUIRE(cc_audio_start(audio, error, sizeof(error)));
    for (unsigned attempt = 0; attempt < 50 && !cc_audio_completed_frames(audio);
         ++attempt) {
        cc_host_sleep(5);
    }
    REQUIRE(cc_audio_completed_frames(audio) > 0);
    REQUIRE(cc_audio_completed_frames(audio) <= 2048);
    bool running;
    REQUIRE(cc_audio_running(audio, &running, error, sizeof(error)));
    REQUIRE(cc_audio_stop(audio, error, sizeof(error)));
    REQUIRE(cc_audio_writable_buffers(audio) == 8);
    REQUIRE(cc_audio_running(audio, &running, error, sizeof(error)) && !running);
    REQUIRE(cc_audio_write(audio, samples, 512, error, sizeof(error)) ==
            CC_AUDIO_QUEUED);
    REQUIRE(cc_audio_stop(audio, error, sizeof(error)));
    cc_audio_destroy(audio);
    cc_audio_destroy(NULL);
    puts("Native PCM queue capacity, callbacks, reset and lifetime passed");
    return 0;
}
