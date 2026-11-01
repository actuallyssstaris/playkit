/*
 * PlayKit - PS5 DualSense input via scePad.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include "ps5.h"
#include "ps5_pad.h"

static int32_t pad_handle = -1;

/* One raw pad sample as the console reports it. */
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
} pad_sample;

_Static_assert(sizeof(pad_sample) == 120, "pad sample size");

bool ps5_pad_start(void)
{
    int32_t user = -1;
    int result;
    int attempt;

    /* scePadOpen needs the id of a logged-in user, and the user service must
     * be initialised before it answers. Opening as user 0 without this fails
     * with 0x809B0081 on firmware 12.02. */
    result = sceUserServiceInitialize(NULL);
    if (result < 0) {
        ps5_log("pad: sceUserServiceInitialize failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    result = sceUserServiceGetInitialUser(&user);
    if (result < 0) {
        ps5_log("pad: sceUserServiceGetInitialUser failed: 0x%08x\n", (uint32_t)result);
        return false;
    }
    ps5_log("pad: user %d\n", user);

    result = scePadInit();
    if (result < 0) {
        ps5_log("pad: scePadInit failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    /* The pad service can publish the device slightly after the title starts.
     * Retry before giving up. */
    for (attempt = 0; attempt < 10; attempt++) {
        result = scePadOpen(user, 0, 0, NULL);
        if (result >= 0) {
            pad_handle = result;
            ps5_log("pad: opened handle %d on attempt %d\n", pad_handle, attempt + 1);
            return true;
        }
        (void)sceKernelUsleep(100000);
    }

    ps5_log("pad: scePadOpen failed: 0x%08x\n", (uint32_t)result);
    return false;
}

void ps5_pad_poll(ps5_pad_state *pad)
{
    static ps5_pad_state previous;
    static bool have_previous = false;

    memset(pad, 0, sizeof(*pad));

    if (pad_handle < 0) {
        return;
    }

    pad_sample sample;
    uint32_t buttons = 0;
    uint8_t left_x = 128, left_y = 128;
    uint8_t right_x = 128, right_y = 128;
    uint8_t l2 = 0, r2 = 0;
    uint32_t connected = 0;

    /* Read all buffered samples, keep the newest. */
    while (1) {
        int result = scePadRead(pad_handle, &sample, 1);
        if (result <= 0) {
            break;
        }
        buttons = sample.buttons;
        left_x = sample.left_x;
        left_y = sample.left_y;
        right_x = sample.right_x;
        right_y = sample.right_y;
        l2 = sample.l2;
        r2 = sample.r2;
        connected = sample.connected;
    }

    pad->connected = connected != 0;
    pad->held = buttons;
    if (have_previous) {
        pad->pressed = buttons & ~previous.held;
    } else {
        pad->pressed = buttons;
    }
    previous.held = buttons;
    have_previous = true;

    /* Normalise sticks to -1..1 (128 at rest, y positive down). */
    pad->left_x = ((float)left_x - 128.0f) / 128.0f;
    pad->left_y = ((float)left_y - 128.0f) / 128.0f;
    pad->right_x = ((float)right_x - 128.0f) / 128.0f;
    pad->right_y = ((float)right_y - 128.0f) / 128.0f;

    /* Triggers 0..1. */
    pad->l2 = (float)l2 / 255.0f;
    pad->r2 = (float)r2 / 255.0f;
}
