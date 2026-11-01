/*
 * PlayKit - WebKit integration layer header.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WEBKIT_GLUE_H
#define WEBKIT_GLUE_H

#include <stdbool.h>

/* Initialize WebKit. */
bool webkit_init(void);

/* Paint one frame into the supplied pixel buffer (SCREEN_WIDTH x SCREEN_HEIGHT
 * ARGB32). */
void webkit_paint_frame(uint32_t *pixels);

/* Load a URL into the page. */
void webkit_load_url(const char *url);

/* Shut down WebKit. */
void webkit_shutdown(void);

#endif
