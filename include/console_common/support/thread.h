#ifndef CC_SUPPORT_THREAD_H
#define CC_SUPPORT_THREAD_H

#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

typedef HANDLE CcThread;
/* Match the nonrecursive POSIX mutex used by the mixers. */
typedef SRWLOCK CcMutex;
typedef CONDITION_VARIABLE CcCondition;

typedef struct {
    void *(*run)(void *);
    void *context;
} CcThreadStart;

static inline unsigned __stdcall cc_thread_entry(void *context) {
    CcThreadStart start = *(CcThreadStart *)context;
    free(context);
    start.run(start.context);
    return 0;
}

static inline int cc_thread_start(CcThread *thread, void *(*run)(void *),
                                  void *context) {
    CcThreadStart *start = malloc(sizeof(*start));
    if (!start)
        return ENOMEM;
    *start = (CcThreadStart){run, context};
    *thread = (HANDLE)_beginthreadex(NULL, 0, cc_thread_entry, start, 0, NULL);
    if (!*thread) {
        free(start);
        return EAGAIN;
    }
    return 0;
}

static inline int cc_thread_join(CcThread thread) {
    DWORD status = WaitForSingleObject(thread, INFINITE);
    if (status != WAIT_OBJECT_0)
        return EINVAL;
    return CloseHandle(thread) ? 0 : EINVAL;
}

static inline int cc_mutex_init(CcMutex *mutex) {
    InitializeSRWLock(mutex);
    return 0;
}
static inline int cc_mutex_destroy(CcMutex *mutex) {
    (void)mutex;
    return 0;
}
static inline int cc_mutex_lock(CcMutex *mutex) {
    AcquireSRWLockExclusive(mutex);
    return 0;
}
static inline int cc_mutex_trylock(CcMutex *mutex) {
    return TryAcquireSRWLockExclusive(mutex) ? 0 : EBUSY;
}
static inline int cc_mutex_unlock(CcMutex *mutex) {
    ReleaseSRWLockExclusive(mutex);
    return 0;
}
static inline int cc_condition_init(CcCondition *condition) {
    InitializeConditionVariable(condition);
    return 0;
}
static inline int cc_condition_destroy(CcCondition *condition) {
    (void)condition;
    return 0;
}
static inline int cc_condition_signal(CcCondition *condition) {
    WakeConditionVariable(condition);
    return 0;
}
static inline int cc_condition_broadcast(CcCondition *condition) {
    WakeAllConditionVariable(condition);
    return 0;
}
static inline int cc_condition_wait(CcCondition *condition, CcMutex *mutex) {
    return SleepConditionVariableSRW(condition, mutex, INFINITE, 0) ? 0 : EINVAL;
}
static inline int cc_condition_timedwait(CcCondition *condition, CcMutex *mutex,
                                         const struct timespec *deadline) {
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC)
        return EINVAL;
    double remaining = (double)(deadline->tv_sec - now.tv_sec) * 1000 +
                       (double)(deadline->tv_nsec - now.tv_nsec) / 1000000;
    DWORD wait = remaining <= 0             ? 0
                 : remaining > 4294967294.0 ? INFINITE - 1
                                            : (DWORD)remaining + 1;
    if (SleepConditionVariableSRW(condition, mutex, wait, 0))
        return 0;
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

#else
#include <pthread.h>

typedef pthread_t CcThread;
typedef pthread_mutex_t CcMutex;
typedef pthread_cond_t CcCondition;

#define cc_thread_start(thread, run, context) pthread_create(thread, NULL, run, context)
#define cc_thread_join(thread) pthread_join(thread, NULL)
#define cc_mutex_init(mutex) pthread_mutex_init(mutex, NULL)
#define cc_mutex_destroy(mutex) pthread_mutex_destroy(mutex)
#define cc_mutex_lock(mutex) pthread_mutex_lock(mutex)
#define cc_mutex_trylock(mutex) pthread_mutex_trylock(mutex)
#define cc_mutex_unlock(mutex) pthread_mutex_unlock(mutex)
#define cc_condition_init(condition) pthread_cond_init(condition, NULL)
#define cc_condition_destroy(condition) pthread_cond_destroy(condition)
#define cc_condition_signal(condition) pthread_cond_signal(condition)
#define cc_condition_broadcast(condition) pthread_cond_broadcast(condition)
#define cc_condition_wait(condition, mutex) pthread_cond_wait(condition, mutex)
#define cc_condition_timedwait(condition, mutex, deadline)                             \
    pthread_cond_timedwait(condition, mutex, deadline)
#endif

#endif
