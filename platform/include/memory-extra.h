#ifndef PLAYKIT_MEMORY_EXTRA_H
#define PLAYKIT_MEMORY_EXTRA_H

/*
 * Sony's libc ships memory-extra.h with a virtual storage space API that
 * libpas uses for its compact heap reservation.  The public payload SDK
 * does not export it, so this shim reserves an ordinary anonymous mapping
 * instead.  The reservation is 128 MiB of address space, small enough that
 * a plain mapping costs nothing until it is touched.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static inline void *memory_extra_vss_reserve(size_t size, size_t alignment)
{
    size_t page = (size_t)sysconf(_SC_PAGE_SIZE);
    size_t total, head, tail;
    uintptr_t raw, base;
    void *mapping;

    if (alignment < page)
        alignment = page;
    if (size % page)
        size += page - (size % page);
    total = size + alignment;

    mapping = mmap(NULL, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapping == MAP_FAILED)
        return NULL;

    raw = (uintptr_t)mapping;
    base = (raw + alignment - 1) & ~(uintptr_t)(alignment - 1);
    head = base - raw;
    tail = total - head - size;

    if (head)
        munmap((void *)raw, head);
    if (tail)
        munmap((void *)(base + size), tail);

    return (void *)base;
}

static inline bool memory_extra_vss_commit(void *ptr, size_t size,
                                           bool protect, int fd)
{
    (void)ptr;
    (void)size;
    (void)protect;
    (void)fd;

    /* The reservation is mapped read/write from the start, so there is
     * nothing left to commit. */
    return true;
}

#endif
