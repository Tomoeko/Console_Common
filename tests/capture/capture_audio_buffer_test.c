#include "console_common/capture/audio_buffer.h"

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { TEST_CAPACITY = 257, TEST_FRAMES = 1000000, TEST_BLOCK = 61 };

typedef struct {
    CcAudioBuffer *buffer;
    atomic_uint_fast64_t consumed;
    atomic_bool producer_finished;
} ConcurrentTest;

static uint32_t sample_word(uint64_t index, unsigned channel) {
    static const uint32_t special[] = {0,          0x80000000, 1,
                                       0x80000001, 0x3f800000, 0xbf800000};
    if (index % 23 < sizeof(special) / sizeof(special[0]))
        return special[(index + channel) % (sizeof(special) / sizeof(special[0]))];
    uint32_t value = (uint32_t)(index * 2654435761U + channel * 2246822519U);
    return (value & 0x807fffffU) | 0x3e800000U;
}

static void fill_samples(float *stereo, uint64_t first, size_t frames) {
    for (size_t frame = 0; frame < frames; ++frame) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            uint32_t word = sample_word(first + frame, channel);
            memcpy(stereo + frame * 2 + channel, &word, sizeof(word));
        }
    }
}

static void check_samples(const float *stereo, uint64_t first, size_t frames) {
    for (size_t frame = 0; frame < frames; ++frame) {
        for (unsigned channel = 0; channel < 2; ++channel) {
            uint32_t word;
            memcpy(&word, stereo + frame * 2 + channel, sizeof(word));
            assert(word == sample_word(first + frame, channel));
        }
    }
}

static void test_bounds_and_empty(void) {
    assert(!cc_audio_buffer_create(0));
    assert(!cc_audio_buffer_create(UINT_MAX));
    assert(!cc_audio_buffer_create(SIZE_MAX));
    assert(!cc_audio_buffer_failed(NULL));
    cc_audio_buffer_destroy(NULL);
    cc_audio_buffer_write(NULL, NULL, 1);
    float stereo[2];
    uint64_t first = UINT64_MAX;
    assert(!cc_audio_buffer_read(NULL, stereo, 1, &first));
    assert(first == UINT64_MAX);

    CcAudioBuffer *buffer = cc_audio_buffer_create(1);
    assert(buffer);
    assert(!cc_audio_buffer_read(buffer, NULL, 1, &first));
    assert(!cc_audio_buffer_read(buffer, stereo, 0, &first));
    assert(!cc_audio_buffer_read(buffer, stereo, SIZE_MAX, &first));
    assert(first == UINT64_MAX);
    assert(!cc_audio_buffer_read(buffer, stereo, 1, &first) && first == 0);
    cc_audio_buffer_write(buffer, NULL, 0);
    assert(!cc_audio_buffer_failed(buffer));
    fill_samples(stereo, 0, 1);
    cc_audio_buffer_write(buffer, stereo, 1);
    assert(cc_audio_buffer_read(buffer, stereo, 1, &first) == 1 && first == 0);
    check_samples(stereo, first, 1);
    assert(!cc_audio_buffer_read(buffer, stereo, 1, &first) && first == 1);
    cc_audio_buffer_destroy(buffer);
}

static void test_wrapping_and_overflow(void) {
    CcAudioBuffer *buffer = cc_audio_buffer_create(7);
    assert(buffer);
    float input[14];
    float output[14];
    uint64_t completed = 0;
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        size_t count = cycle % 7 + 1;
        fill_samples(input, completed, count);
        cc_audio_buffer_write(buffer, input, count);
        uint64_t first = UINT64_MAX;
        size_t initial = count > 3 ? 3 : count;
        assert(cc_audio_buffer_read(buffer, output, initial, &first) == initial);
        assert(first == completed);
        check_samples(output, first, initial);
        completed += initial;
        size_t remainder = count - initial;
        assert(cc_audio_buffer_read(buffer, output, 7, &first) == remainder);
        assert(first == completed);
        check_samples(output, first, remainder);
        completed += remainder;
        assert(!cc_audio_buffer_failed(buffer));
    }
    fill_samples(input, completed, 7);
    cc_audio_buffer_write(buffer, input, 7);
    cc_audio_buffer_write(buffer, input, 1);
    assert(cc_audio_buffer_failed(buffer));
    uint64_t first;
    assert(cc_audio_buffer_read(buffer, output, 7, &first) == 7);
    assert(first == completed);
    check_samples(output, first, 7);
    cc_audio_buffer_write(buffer, input, 1);
    assert(!cc_audio_buffer_read(buffer, output, 7, NULL));
    cc_audio_buffer_destroy(buffer);

    buffer = cc_audio_buffer_create(7);
    assert(buffer);
    cc_audio_buffer_write(buffer, input, 2);
    cc_audio_buffer_write(buffer, NULL, 1);
    assert(cc_audio_buffer_failed(buffer));
    assert(cc_audio_buffer_read(buffer, output, 7, &first) == 2 && first == 0);
    assert(!memcmp(input, output, 4 * sizeof(float)));
    cc_audio_buffer_destroy(buffer);
}

static void *produce_samples(void *context) {
    ConcurrentTest *test = context;
    float stereo[TEST_BLOCK * 2];
    uint64_t produced = 0;
    while (produced < TEST_FRAMES) {
        size_t count = (size_t)(produced % TEST_BLOCK) + 1;
        if (count > TEST_FRAMES - produced)
            count = (size_t)(TEST_FRAMES - produced);
        uint64_t consumed = atomic_load_explicit(&test->consumed, memory_order_acquire);
        if (produced - consumed + count > TEST_CAPACITY) {
            sched_yield();
            continue;
        }
        fill_samples(stereo, produced, count);
        cc_audio_buffer_write(test->buffer, stereo, count);
        assert(!cc_audio_buffer_failed(test->buffer));
        produced += count;
    }
    atomic_store_explicit(&test->producer_finished, true, memory_order_release);
    return NULL;
}

static void *consume_samples(void *context) {
    ConcurrentTest *test = context;
    float stereo[TEST_BLOCK * 2];
    uint64_t completed = 0;
    while (completed < TEST_FRAMES) {
        size_t capacity = (size_t)((completed * 7) % TEST_BLOCK) + 1;
        uint64_t first = UINT64_MAX;
        size_t count = cc_audio_buffer_read(test->buffer, stereo, capacity, &first);
        assert(first == completed && count <= capacity);
        if (!count) {
            sched_yield();
            continue;
        }
        check_samples(stereo, first, count);
        completed += count;
        atomic_store_explicit(&test->consumed, completed, memory_order_release);
    }
    while (!atomic_load_explicit(&test->producer_finished, memory_order_acquire))
        sched_yield();
    assert(!cc_audio_buffer_read(test->buffer, stereo, TEST_BLOCK, NULL));
    return NULL;
}

static void test_concurrent_exactness(void) {
    ConcurrentTest test = {.buffer = cc_audio_buffer_create(TEST_CAPACITY)};
    assert(test.buffer);
    atomic_init(&test.consumed, 0);
    atomic_init(&test.producer_finished, false);
    pthread_t producer;
    pthread_t consumer;
    assert(pthread_create(&producer, NULL, produce_samples, &test) == 0);
    assert(pthread_create(&consumer, NULL, consume_samples, &test) == 0);
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(atomic_load_explicit(&test.consumed, memory_order_relaxed) == TEST_FRAMES);
    assert(!cc_audio_buffer_failed(test.buffer));
    cc_audio_buffer_destroy(test.buffer);
}

int main(void) {
    test_bounds_and_empty();
    test_wrapping_and_overflow();
    test_concurrent_exactness();
    puts("Shared audio buffer tests passed.");
    return 0;
}
