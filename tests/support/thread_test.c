#include "console_common/support/thread.h"

#include <assert.h>
#include <stdbool.h>

typedef struct {
    CcMutex mutex;
    CcCondition condition;
    bool ready;
    bool released;
} ThreadFixture;

static void *wait_for_release(void *context) {
    ThreadFixture *fixture = context;
    assert(cc_mutex_lock(&fixture->mutex) == 0);
    fixture->ready = true;
    assert(cc_condition_signal(&fixture->condition) == 0);
    while (!fixture->released)
        assert(cc_condition_wait(&fixture->condition, &fixture->mutex) == 0);
    assert(cc_mutex_unlock(&fixture->mutex) == 0);
    return NULL;
}

int main(void) {
    ThreadFixture fixture = {0};
    assert(cc_mutex_init(&fixture.mutex) == 0);
    assert(cc_condition_init(&fixture.condition) == 0);
    assert(cc_mutex_lock(&fixture.mutex) == 0);
    assert(cc_mutex_trylock(&fixture.mutex) == EBUSY);
    struct timespec deadline;
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    --deadline.tv_sec;
    assert(cc_condition_timedwait(&fixture.condition, &fixture.mutex, &deadline) ==
           ETIMEDOUT);
    CcThread thread;
    assert(cc_thread_start(&thread, wait_for_release, &fixture) == 0);
    while (!fixture.ready)
        assert(cc_condition_wait(&fixture.condition, &fixture.mutex) == 0);
    fixture.released = true;
    assert(cc_condition_broadcast(&fixture.condition) == 0);
    assert(cc_mutex_unlock(&fixture.mutex) == 0);
    assert(cc_thread_join(thread) == 0);
    assert(cc_mutex_destroy(&fixture.mutex) == 0);
    assert(cc_condition_destroy(&fixture.condition) == 0);
    return 0;
}
