/*
 * PlayKit - site icons for the home screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fetches /favicon.ico for each home-screen site over libcurl, decodes ICO
 * (PNG or uncompressed DIB entries) and PNG, scales to 64x64 and keeps the
 * result in memory. Decoded icons are cached under the title's writable area
 * so the next launch draws them without a request.
 *
 * The UI never waits on the network: favicons_get returns NULL until an icon
 * is ready, and the caller draws its fallback.
 */

#ifndef PLAYKIT_FAVICONS_H
#define PLAYKIT_FAVICONS_H

#include <stdbool.h>
#include <stdint.h>

#define FAVICON_SIZE 64

/* Starts the worker. cache_dir is created if missing. Safe to call once. */
bool favicons_init(const char *cache_dir);
void favicons_shutdown(void);

/* Queues a page URL for a fetch when its icon is not cached yet. */
void favicons_prefetch(const char *url);

/* 64x64 pixels in VideoOut byte order (0xAABBGGRR), or NULL while pending. */
const uint32_t *favicons_get(const char *url);

#endif
