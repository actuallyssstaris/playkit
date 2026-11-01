/*
 * PlayKit - tiled VideoOut frame layout.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console's display buffers are tiled, not linear: 64 KiB blocks of
 * 128x128 pixels, pixel groups swizzled inside each block. This is the
 * byte offset of the 4-pixel group at (x, y) in a 1920-wide frame; the
 * layout is the one PlaySurf validated on hardware.
 *
 * The math is pure and stays in the header so host tests can reuse it.
 */

#ifndef PS5_TILING_H
#define PS5_TILING_H

#include <stddef.h>
#include <stdint.h>

#define PS5_FRAME_WIDTH 1920
#define PS5_FRAME_HEIGHT 1080
/* Console buffer allocation. The tiled layout needs more than the linear
 * 1920*1080*4 bytes: it is 9 rows x 15 columns of 64 KiB blocks. */
#define PS5_FRAME_BYTES 0x1000000
/* Bytes actually reachable through the tiling. */
#define PS5_TILED_USED_BYTES                                                                       \
    ((((PS5_FRAME_HEIGHT + 127) / 128) * ((PS5_FRAME_WIDTH + 127) / 128)) << 16)

static inline size_t ps5_tiled_group_offset(uint32_t x, uint32_t y)
{
    const uint32_t y_part =
        ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^ ((y << 8) & 0x4000U);
    const uint32_t x_part = ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^ ((x << 4) & 0x400U) ^
                            ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const uint32_t blocks_per_row = (PS5_FRAME_WIDTH + 127U) >> 7;
    const uint32_t block = (y >> 7) * blocks_per_row + (x >> 7);

    return ((size_t)block << 16) + (y_part ^ x_part);
}

#endif
