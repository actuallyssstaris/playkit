/*
 * PlayKit - WebKit browser for PlayStation 5.
 * PS5 platform declarations.
 *
 * The console SDK ships import stubs (libScePad.so, libSceVideoOut.so, ...)
 * but no headers for these, so the ABI used here is declared explicitly.
 * The pad layout and button bits are the ones validated on hardware by the
 * PS5 Vulkan Template project and by PlaySurf.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_H
#define PS5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* logging                                                             */

    /* Appends one formatted line to the title's log file, <state dir>/playkit.log
     * (the game folder on a dev console, /download0 otherwise; /app0 as the last
     * resort). The console has no terminal for the title. */
    void ps5_log(const char *format, ...);

    /* ------------------------------------------------------------------ */
    /* console imports                                                     */

    int64_t sceKernelGetDirectMemorySize(void);
    int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length,
                                      size_t alignment, int memory_type, int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                                 int64_t physical_address, size_t alignment);
    int sceKernelUsleep(uint32_t microseconds);
    int sceSystemServiceHideSplashScreen(void);
    /* mode is "exit" to return to the loader/home screen. */
    int sceSystemServiceLoadExec(const char *mode, void *param);

    int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
    int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
    int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index, uint32_t flip_mode,
                              int64_t flip_argument);
    int sceVideoOutWaitVblank(int32_t handle);

    typedef struct
    {
        void *data;
        void *metadata;
        void *reserved0;
        void *reserved1;
    } sceVideoOutBuffer;

    typedef struct
    {
        uint8_t reserved[80];
    } sceVideoOutAttribute;

    void sceVideoOutSetBufferAttribute2(sceVideoOutAttribute *attribute, uint64_t pixel_format,
                                        uint32_t tiling_mode, uint32_t width, uint32_t height,
                                        uint64_t option, uint32_t dcc_control,
                                        uint64_t dcc_clear_color);
    int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index, int32_t buffer_index_start,
                                    sceVideoOutBuffer *buffers, int32_t buffer_count,
                                    sceVideoOutAttribute *attribute, int32_t category,
                                    void *option);

    int scePadInit(void);
    int scePadOpen(int32_t user_id, int32_t type, int32_t index, void *param);
    int scePadClose(int32_t handle);
    int scePadRead(int32_t handle, void *data, int count);

    int sceUserServiceInitialize(void *params);
    int sceUserServiceGetInitialUser(int32_t *user_id);

    /* ------------------------------------------------------------------ */
    /* video                                                               */

    bool ps5_video_start(void);
    void ps5_video_present(const uint32_t *frame, int stride32);
    void ps5_video_stop(void);

    /* ------------------------------------------------------------------ */
    /* pad                                                                 */

    enum
    {
        PS5_PAD_L3 = 0x000002,
        PS5_PAD_R3 = 0x000004,
        PS5_PAD_OPTIONS = 0x000008,
        PS5_PAD_UP = 0x000010,
        PS5_PAD_RIGHT = 0x000020,
        PS5_PAD_DOWN = 0x000040,
        PS5_PAD_LEFT = 0x000080,
        PS5_PAD_L1 = 0x000400,
        PS5_PAD_R1 = 0x000800,
        PS5_PAD_TRIANGLE = 0x001000,
        PS5_PAD_CIRCLE = 0x002000,
        PS5_PAD_CROSS = 0x004000,
        PS5_PAD_SQUARE = 0x008000,
        PS5_PAD_TOUCH_PAD = 0x100000,
    };
#define PS5_PAD_INTERCEPTED 0x80000000u

    typedef struct
    {
        uint32_t buttons;
        uint8_t left_x, left_y;
        uint8_t right_x, right_y;
        uint8_t l2, r2;
        uint8_t reserved[0x42];
        uint32_t connected;
        uint64_t timestamp_us;
        uint8_t tail[120 - 0x58];
    } ps5_pad_reading;

    _Static_assert(sizeof(ps5_pad_reading) == 120, "pad reading size");
    _Static_assert(offsetof(ps5_pad_reading, connected) == 0x4c, "connected offset");

    typedef struct
    {
        uint32_t held;
        uint32_t pressed;
        float left_x, left_y, right_x, right_y;
        float l2, r2;
        bool connected;
    } ps5_pad_state;

    bool ps5_pad_start(void);
    void ps5_pad_poll(ps5_pad_state *pad);

#ifdef __cplusplus
}
#endif

#endif
