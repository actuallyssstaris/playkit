/*
 * PlayKit - PS5 video output via VideoOut.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Presents frames through the console's display controller: two tiled
 * RGBA8 buffers in write-combined direct memory, registered with
 * sceVideoOutRegisterBuffers2, flipped at vblank. The tiler converts the
 * linear frame the renderer produces into the tiled layout the hardware
 * reads. PlaySurf validated this layout on hardware.
 *
 * A future OpenGL backend keeps this same ps5_video_present() interface;
 * it replaces the software tiler with eglSwapBuffers without changing the
 * rest of the application.
 */

#include <string.h>

#include "ps5.h"
#include "ps5_tiling.h"
#include "ps5_video.h"

#define FRAME_WIDTH PS5_FRAME_WIDTH
#define FRAME_HEIGHT PS5_FRAME_HEIGHT
#define FRAME_BYTES PS5_FRAME_BYTES
#define FRAME_MEMORY (FRAME_BYTES * 2)
#define MEMORY_ALIGNMENT 0x200000
#define MEMORY_TYPE_WC_GARLIC 3
#define MAP_PROTECTION 0x33
#define PIXEL_FORMAT_RGBA8_SRGB UINT64_C(0x8000000022000000)

static int video_handle = -1;
static sceVideoOutBuffer video_buffers[2];
static uint8_t *video_memory;
static int video_front;
static uint64_t video_frame_index;

bool ps5_video_start(void)
{
    int64_t physical = 0;
    int64_t pool = sceKernelGetDirectMemorySize();
    sceVideoOutAttribute attribute;
    int result;

    if (pool < FRAME_MEMORY) {
        ps5_log("video: direct memory too small (%lld < %d)\n",
                (long long)pool, (int)FRAME_MEMORY);
        return false;
    }

    result = sceKernelAllocateDirectMemory(0, pool, FRAME_MEMORY, MEMORY_ALIGNMENT,
                                           MEMORY_TYPE_WC_GARLIC, &physical);
    if (result < 0) {
        ps5_log("video: sceKernelAllocateDirectMemory failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    result = sceKernelMapDirectMemory((void **)&video_memory, FRAME_MEMORY, MAP_PROTECTION, 0,
                                      physical, MEMORY_ALIGNMENT);
    if (result < 0) {
        ps5_log("video: sceKernelMapDirectMemory failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    video_handle = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (video_handle < 0) {
        ps5_log("video: sceVideoOutOpen failed: 0x%08x\n", (uint32_t)video_handle);
        return false;
    }

    video_buffers[0].data = video_memory;
    video_buffers[1].data = video_memory + FRAME_BYTES;
    video_buffers[0].metadata = NULL;
    video_buffers[1].metadata = NULL;
    video_buffers[0].reserved0 = NULL;
    video_buffers[1].reserved0 = NULL;
    video_buffers[0].reserved1 = NULL;
    video_buffers[1].reserved1 = NULL;

    memset(video_memory, 0, FRAME_MEMORY);

    (void)sceVideoOutSetFlipRate(video_handle, 0);
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_RGBA8_SRGB, 0, FRAME_WIDTH,
                                   FRAME_HEIGHT, 0, 0, 0);
    result = sceVideoOutRegisterBuffers2(video_handle, 0, 0, video_buffers, 2, &attribute, 0, NULL);
    if (result < 0) {
        ps5_log("video: sceVideoOutRegisterBuffers2 failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    /* Show the black first buffer before the splash is hidden. */
    result = sceVideoOutSubmitFlip(video_handle, 0, 1, 0);
    if (result < 0) {
        ps5_log("video: first sceVideoOutSubmitFlip failed: 0x%08x\n", (uint32_t)result);
        return false;
    }
    (void)sceVideoOutWaitVblank(video_handle);

    video_front = 0;
    video_frame_index = 1;

    ps5_log("video: %d byte buffers registered, %d byte direct pool\n",
            (int)FRAME_MEMORY, (int)pool);
    return true;
}

static void flush_range(void *address, size_t length)
{
    uint8_t *at = address;
    const uint8_t *end = at + length;

    for (; at < end; at += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    }
    __asm__ volatile("mfence" : : : "memory");
}

void ps5_video_present(const uint32_t *frame, int stride32)
{
    const int back = video_front ^ 1;
    uint8_t *dst;

    if (video_handle < 0)
        return;

    dst = video_buffers[back].data;

    for (int y = 0; y < FRAME_HEIGHT; y++) {
        const uint32_t *src = frame + (size_t)y * stride32;
        for (int x = 0; x < FRAME_WIDTH; x += 4) {
            memcpy(dst + ps5_tiled_group_offset(x, y), src + x, 16);
        }
    }

    flush_range(dst, FRAME_BYTES);
    if (sceVideoOutSubmitFlip(video_handle, back, 1, video_frame_index) < 0) {
        /* The previous flip is still queued; do not overrun it. */
        (void)sceKernelUsleep(4000);
    }
    video_frame_index++;
    (void)sceVideoOutWaitVblank(video_handle);
    video_front = back;
}

void ps5_video_stop(void)
{
    if (video_handle >= 0) {
        (void)sceVideoOutWaitVblank(video_handle);
        video_handle = -1;
    }
}
