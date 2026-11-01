/*
 * PlayKit - process-lifetime heap for the console application.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_HEAP_H
#define PS5_HEAP_H

#include <stddef.h>

/* Initialise the sceLibcMspace heap over console flexible memory. */
void ps5_heap_init(void);

/* Heap capacity in bytes (0 if the flexible-memory mapping could not be made). */
size_t ps5_heap_capacity(void);

/* Current live bytes, peak bytes, and allocation failures. */
void ps5_heap_stats(size_t *live_bytes, size_t *peak_bytes, size_t *failures);

#endif
