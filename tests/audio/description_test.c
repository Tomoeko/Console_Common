#include "console_common/platform/audio.h"

#include <stdio.h>
#include <stdlib.h>

static void require(bool condition, int line, const char *expression) {
    if (!condition) {
        fprintf(stderr, "audio_description_test.c:%d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define REQUIRE(expression) require((expression), __LINE__, #expression)

int main(void) {
    CcAudioDescription description = {48000, 8, 256, 8};
    size_t size = 7;
    char error[128];
    REQUIRE(cc_audio_description_validate(&description, &size, error, sizeof(error)));
    REQUIRE(size == 8192);
    description.channels = 2;
    REQUIRE(cc_audio_description_validate(&description, &size, NULL, 0));
    REQUIRE(size == 2048);
    description = (CcAudioDescription){384000, 32, 8192, 64};
    REQUIRE(cc_audio_description_validate(&description, &size, NULL, 0));
    REQUIRE(size == 1048576);
    const CcAudioDescription invalid[] = {
        {0, 2, 256, 8},   {384001, 2, 256, 8}, {48000, 0, 256, 8}, {48000, 33, 256, 8},
        {48000, 2, 0, 8}, {48000, 2, 8193, 8}, {48000, 2, 256, 0}, {48000, 2, 256, 65}};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(*invalid); ++index) {
        size = 7;
        REQUIRE(!cc_audio_description_validate(invalid + index, &size, error,
                                               sizeof(error)));
        REQUIRE(size == 7 && error[0]);
    }
    REQUIRE(!cc_audio_description_validate(NULL, &size, NULL, 0));
    REQUIRE(!cc_audio_description_validate(&description, NULL, NULL, 0));
    puts("Audio dimensions and allocation bounds passed");
    return 0;
}
