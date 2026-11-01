#define _GNU_SOURCE
#include "ps5_compat.h"

#include <errno.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

int timespec_get(struct timespec *ts, int base)
{
    if (base != TIME_UTC || ts == NULL) {
        return 0;
    }
    if (clock_gettime(CLOCK_REALTIME, ts) != 0) {
        return 0;
    }
    return base;
}

int pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           clockid_t clock, const struct timespec *abstime)
{
    struct timespec now;
    struct timespec realtime;

    if (clock == CLOCK_REALTIME) {
        return pthread_cond_timedwait(cond, mutex, abstime);
    }

    if (clock_gettime(clock, &now) != 0) {
        return errno;
    }

    int64_t remaining_ns =
        ((int64_t)abstime->tv_sec - (int64_t)now.tv_sec) * 1000000000LL +
        ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec);
    if (remaining_ns < 0) {
        remaining_ns = 0;
    }

    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0) {
        return errno;
    }

    realtime.tv_sec += remaining_ns / 1000000000LL;
    realtime.tv_nsec += remaining_ns % 1000000000LL;
    if (realtime.tv_nsec >= 1000000000L) {
        realtime.tv_sec++;
        realtime.tv_nsec -= 1000000000L;
    }

    return pthread_cond_timedwait(cond, mutex, &realtime);
}

#if defined(__cplusplus)
}
#endif
