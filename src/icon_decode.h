/*
 * PlayKit - icon decoding for site favicons.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Decodes the formats a site actually serves as a favicon: PNG, and ICO
 * containers holding PNG or uncompressed DIB entries (1/4/8/16/24/32 bpp,
 * with the AND mask for transparency). The result is always a 64x64 RGBA
 * image in VideoOut byte order (0xAABBGGRR).
 */

#ifndef PLAYKIT_ICON_DECODE_H
#define PLAYKIT_ICON_DECODE_H

#include <stddef.h>
#include <stdint.h>

#define PLAYKIT_ICON_SIZE 64

/* Returns a malloc'd 64x64 image, or NULL when the bytes are not a supported
 * icon. The caller owns the buffer. */
uint32_t *playkit_icon_decode(const unsigned char *data, size_t length);

#endif
