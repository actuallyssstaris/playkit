/*
 * PlayKit - process-lifetime heap for the console application.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console hands a title a libc heap of about 13 MiB. WebKit needs far
 * more: the 1920x1080 Cairo surface alone is 8 MiB, plus DOM, style, layout,
 * and JavaScriptCore state. The malloc family is redirected at link time
 * (--wrap=malloc ...) into a fixed sceLibcMspace over console flexible
 * memory. The mapping is never released: C++ destructors and atexit handlers
 * still free memory after the last page is gone.
 *
 * Pointers that do not belong to this heap are passed to the console's own
 * allocator, so memory from libc internals keeps working.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ps5.h"
#include "ps5_heap.h"

/* The application log, defined in main.c. */
void ps5_log(const char *format, ...);

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size, unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment, size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

/* Largest mapping first: a page with 1080p video and WebKit DOM needs well
 * over 128 MiB. The smaller sizes are fallbacks. */
static const size_t ps5_heap_sizes[] = {
    384u * 1024u * 1024u,
    256u * 1024u * 1024u,
    128u * 1024u * 1024u,
};

/* Console flexible memory: the mapping the verified console paths use for
 * CPU-visible buffers. An anonymous mmap kills a sandboxed title, so this is
 * the only way to a heap bigger than the one the loader hands out. */
extern int sceKernelMapNamedFlexibleMemory(void **address, size_t length,
                                           int protection, int flags,
                                           const char *name);
extern int sceKernelReleaseFlexibleMemory(void *address, size_t length);
#define FLEXIBLE_CPU_RW 0x03

static atomic_int heap_state; /* 0 unstarted, 1 starting, 2 ready, -1 failed */
static void *heap_base;
static void *heap_mspace;
static size_t heap_size;

static atomic_size_t heap_live_bytes;
static atomic_size_t heap_peak_bytes;
static atomic_size_t heap_failures;

static int heap_ready(void)
{
    int state = atomic_load_explicit(&heap_state, memory_order_acquire);

    if (state == 2) {
        return 1;
    }
    if (state != 0) {
        return 0;
    }

    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&heap_state, &expected, 1,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        return expected == 2;
    }

    void *base = NULL;
    for (size_t i = 0; i < sizeof(ps5_heap_sizes) / sizeof(ps5_heap_sizes[0]); i++) {
        void *mapping = NULL;
        int result;

        result = sceKernelMapNamedFlexibleMemory(&mapping, ps5_heap_sizes[i],
                                                 FLEXIBLE_CPU_RW, 0, "PlayKit");
        if (result == 0 && mapping != NULL) {
            base = mapping;
            heap_size = ps5_heap_sizes[i];
            break;
        }
    }
    if (base == NULL) {
        ps5_log("heap: no flexible mapping, keeping the console heap\n");
        atomic_store_explicit(&heap_state, -1, memory_order_release);
        return 0;
    }

    heap_base = base;
    heap_mspace = sceLibcMspaceCreate("PlayKit", base, heap_size, 0);
    if (heap_mspace == NULL) {
        sceKernelReleaseFlexibleMemory(base, heap_size);
        heap_base = NULL;
        heap_size = 0;
        atomic_store_explicit(&heap_state, -1, memory_order_release);
        return 0;
    }

    atomic_store_explicit(&heap_state, 2, memory_order_release);
    return 1;
}

static int heap_owns(const void *address)
{
    if (atomic_load_explicit(&heap_state, memory_order_acquire) != 2) {
        return 0;
    }
    uintptr_t value = (uintptr_t)address;
    uintptr_t base = (uintptr_t)heap_base;

    return value >= base && value - base < heap_size;
}

static void heap_account(size_t before, size_t after)
{
    size_t live;

    if (after >= before) {
        live = atomic_fetch_add_explicit(&heap_live_bytes, after - before,
                                         memory_order_relaxed) + (after - before);
    } else {
        live = atomic_fetch_sub_explicit(&heap_live_bytes, before - after,
                                         memory_order_relaxed) - (before - after);
    }

    size_t peak = atomic_load_explicit(&heap_peak_bytes, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&heap_peak_bytes, &peak, live,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static void *heap_note(void *address, int nonzero)
{
    if (address != NULL) {
        heap_account(0, sceLibcMspaceMallocUsableSize(address));
    } else if (nonzero) {
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    }
    return address;
}

void *__wrap_malloc(size_t size)
{
    if (!heap_ready()) {
        return __real_malloc(size);
    }
    return heap_note(sceLibcMspaceMalloc(heap_mspace, size), size != 0);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (!heap_ready()) {
        return __real_calloc(count, size);
    }
    return heap_note(sceLibcMspaceCalloc(heap_mspace, count, size),
                     count != 0 && size != 0);
}

void *__wrap_realloc(void *address, size_t size)
{
    if (address == NULL) {
        return __wrap_malloc(size);
    }
    if (!heap_owns(address)) {
        return __real_realloc(address, size);
    }

    size_t before = sceLibcMspaceMallocUsableSize(address);
    void *result = sceLibcMspaceRealloc(heap_mspace, address, size);

    if (result != NULL) {
        heap_account(before, sceLibcMspaceMallocUsableSize(result));
    } else if (size != 0) {
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    }
    return result;
}

void __wrap_free(void *address)
{
    if (address != NULL && heap_owns(address)) {
        heap_account(sceLibcMspaceMallocUsableSize(address), 0);
        sceLibcMspaceFree(heap_mspace, address);
        return;
    }
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size)
{
    if (!heap_ready()) {
        return __real_posix_memalign(address, alignment, size);
    }

    int result = sceLibcMspacePosixMemalign(heap_mspace, address, alignment, size);
    if (result == 0) {
        heap_note(*address, size != 0);
    } else if (result == ENOMEM) {
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    }
    return result;
}

void *__wrap_memalign(size_t alignment, size_t size)
{
    void *address = NULL;

    if (__wrap_posix_memalign(&address, alignment, size) != 0) {
        return NULL;
    }
    return address;
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    return __wrap_memalign(alignment, size);
}

size_t __wrap_malloc_usable_size(const void *address)
{
    return heap_owns(address) ? sceLibcMspaceMallocUsableSize(address)
                              : __real_malloc_usable_size(address);
}

void ps5_heap_init(void)
{
    (void)heap_ready();
}

size_t ps5_heap_capacity(void)
{
    return atomic_load_explicit(&heap_state, memory_order_acquire) == 2 ? heap_size : 0;
}

void ps5_heap_stats(size_t *live_bytes, size_t *peak_bytes, size_t *failures)
{
    if (live_bytes != NULL) {
        *live_bytes = atomic_load_explicit(&heap_live_bytes, memory_order_relaxed);
    }
    if (peak_bytes != NULL) {
        *peak_bytes = atomic_load_explicit(&heap_peak_bytes, memory_order_relaxed);
    }
    if (failures != NULL) {
        *failures = atomic_load_explicit(&heap_failures, memory_order_relaxed);
    }
}
