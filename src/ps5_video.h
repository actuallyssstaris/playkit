/*
 * PlayKit - PS5 video output via VideoOut.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_VIDEO_H
#define PS5_VIDEO_H

#include <stdint.h>

#include "ps5.h"

/* Open 1920x1080 VideoOut output with two tiled RGBA8 buffers. */
bool ps5_video_start(void);

/* Copy one linear 1920x1080 32bpp frame into the off-screen tiled buffer
 * and flip at the next vblank. */
void ps5_video_present(const uint32_t *frame, int stride32);

void ps5_video_stop(void);

#endif
