#ifndef PLAYKIT_PS5_COMPAT_H
#define PLAYKIT_PS5_COMPAT_H

#include <pthread.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The console libc has pthread_cond_timedwait but no
 * pthread_cond_clockwait, which libc++ v2 calls.  Define the missing entry
 * point here so C++ standard library headers compile.
 */
int pthread_cond_clockwait(pthread_cond_t *, pthread_mutex_t *, clockid_t,
                           const struct timespec *);

#ifndef TIME_UTC
#define TIME_UTC 1
#endif
int timespec_get(struct timespec *, int);

/*
 * libpas in bmalloc names its mutexes through pthread_mutex_setname_np.
 * The payload SDK header does not declare it, and libkernel exports the
 * name as an alias of pthread_set_name_np(pthread_t, ...), so calling it
 * with a mutex pointer would set a bogus thread name.  Lock naming is
 * diagnostic only, so compile the call away.
 */
#define pthread_mutex_setname_np(mutex, name) ((void)0)

/*
 * The SDK names the thread naming entry point pthread_set_name_np; Sony's
 * code and libpas call it pthread_setname_np, sometimes for the current
 * thread (one argument) and sometimes for a given thread (two arguments).
 * Accept both forms.
 */
#define PLAYKIT_SETNAME_1(name) pthread_set_name_np(pthread_self(), (name))
#define PLAYKIT_SETNAME_2(thread, name) pthread_set_name_np((thread), (name))
#define PLAYKIT_SETNAME_PICK(_1, _2, NAME, ...) NAME
#define pthread_setname_np(...) \
    PLAYKIT_SETNAME_PICK(__VA_ARGS__, PLAYKIT_SETNAME_2, PLAYKIT_SETNAME_1)(__VA_ARGS__)

/*
 * Sony's libc adds clock_gettime_np with fast monotonic clocks.  The public
 * SDK only exports clock_gettime, so use the plain monotonic clock there.
 */
#define clock_gettime_np(id, tp) \
    clock_gettime((id) == CLOCK_MONOTONIC_FAST ? CLOCK_MONOTONIC : (id), (tp))

/*
 * WebKit selects its PlayStation port through WTF_PLATFORM_PLAYSTATION.  The
 * JSCOnly CMake port does not set it, so Unix-only code such as
 * dl_iterate_phdr in JSCBytecodeCacheVersion.cpp was compiled.  The console
 * libc declares that function in sys/link_elf.h but exports neither it nor
 * ElfW, and the link would fail.  Defining the port here selects the
 * PlayStation branches, which use a precomputed cache version instead.
 */
#ifndef WTF_PLATFORM_PLAYSTATION
#define WTF_PLATFORM_PLAYSTATION 1
#endif

#ifdef __cplusplus
}
#endif

#endif
