/*
 * PlayKit - WebKit integration layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bridge between the native PS5 application and WebKit.
 *
 * Phase 2: paints a diagnostic screen directly into the frame buffer
 * (no external rendering library yet). Once libJavaScriptCore.a and the
 * WPEWebKit static libraries are built, replace the paint routine with
 * real WKPagePaint calls into a Cairo surface.
 */

#include <string.h>

#include "ps5.h"
#include "ps5_paths.h"
#include "webkit_glue.h"

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080
#define WIDTH  SCREEN_WIDTH
#define HEIGHT SCREEN_HEIGHT

static bool g_webkit_initialized = false;

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */

static void paint_diagnostic(uint32_t *pixels)
{
    /* VideoOut reads RGBA8 bytes, so a little-endian uint32 is 0xAABBGGRR.
     * Dark slate background (RGB 2a 2f 38). */
    for (size_t i = 0; i < (size_t)WIDTH * HEIGHT; i++)
        pixels[i] = 0xff382f2au;

    /* Blue border (RGB 00 a2 ff). */
    for (int y = 20; y < HEIGHT - 20; y++) {
        pixels[y * WIDTH + 20] = 0xffffa200u;
        pixels[y * WIDTH + WIDTH - 21] = 0xffffa200u;
    }
    for (int x = 20; x < WIDTH - 20; x++) {
        pixels[20 * WIDTH + x] = 0xffffa200u;
        pixels[(HEIGHT - 21) * WIDTH + x] = 0xffffa200u;
    }

    /* Crosshairs at centre. */
    int cx = WIDTH / 2;
    int cy = HEIGHT / 2;
    for (int y = 0; y < HEIGHT; y++)
        pixels[y * WIDTH + cx] = 0xffffa200u;
    for (int x = 0; x < WIDTH; x++)
        pixels[cy * WIDTH + x] = 0xffffa200u;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */

bool webkit_init(void)
{
    if (g_webkit_initialized)
        return true;

    g_webkit_initialized = true;
    ps5_log("webkit: initialized (diagnostic renderer)\n");
    return true;
}

void webkit_paint_frame(uint32_t *pixels)
{
    if (!g_webkit_initialized)
        return;

    paint_diagnostic(pixels);
}

void webkit_load_url(const char *url)
{
    if (!g_webkit_initialized || !url)
        return;

    ps5_log("webkit: load_url %s\n", url);

    /* TODO: once WPEWebKit is linked, replace this with a real
     * WKPageLoadURL call. For now the URL is logged only. */
}

void webkit_mouse_click(int x, int y)
{
    if (!g_webkit_initialized)
        return;

    ps5_log("webkit: mouse click %d,%d\n", x, y);

    /* TODO: once WPEWebKit is linked, forward this as a pointer press and
     * release through the view's input handler. */
}

void webkit_shutdown(void)
{
    if (!g_webkit_initialized)
        return;

    g_webkit_initialized = false;
    ps5_log("webkit: shutdown\n");
}
