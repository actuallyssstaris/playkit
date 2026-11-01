/*
 * PlayKit - WebKit browser for PlayStation 5.
 * Native application entry point and run loop.
 *
 * Runs as a standalone PS5 title (not a payload). Entry point is int main(void)
 * via the native-app startup object. On quit, returns to the loader with
 * sceSystemServiceLoadExec("exit").
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "browser_ui.h"
#include "favicons.h"
#include "ps5.h"
#include "ps5_heap.h"
#include "ps5_paths.h"
#include "ps5_pad.h"
#include "ps5_video.h"
#include "webkit_glue.h"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

static FILE *log_stream;
static bool playkit_complete;

void ps5_log(const char *format, ...)
{
    va_list ap;

    if (log_stream == NULL) {
        char path[300];

        ps5_state_path(path, sizeof(path), "playkit.log");
        log_stream = fopen(path, "w");
        if (log_stream == NULL) {
            log_stream = fopen("/app0/playkit.log", "w");
        }
        if (log_stream == NULL) {
            return;
        }
        setvbuf(log_stream, NULL, _IOLBF, 0);
    }

    va_start(ap, format);
    vfprintf(log_stream, format, ap);
    va_end(ap);
}

/* Leave through the loader. A native title must never return from main():
 * the startup object then calls the console libc's exit(), which raises
 * SIGSYS in this launch context (report CE-108255-1). */
static _Noreturn void app_exit(void)
{
    (void)sceSystemServiceLoadExec("exit", NULL);
    for (;;) {
        (void)sceKernelUsleep(1000000);
    }
}

/* ------------------------------------------------------------------------ */
/* main loop                                                             */

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void ensure_writable_state(void)
{
    /* One folder for everything the app writes: the game folder when the title
     * runs from /data/homebrew, otherwise /download0. */
    (void)ps5_state_dir();
    setenv("HOME", ps5_state_dir(), 1);
}

static const char *const shell_font_dirs[] = {"/app0/assets/fonts", "assets/fonts"};

/* Whatever the shell asked for, done to the engine. Until WebKit is linked
 * the shell still draws and reports honestly instead of pretending a page
 * loaded. */
static void shell_actions(void)
{
    char url[1024];
    browser_ui_action action;
    int index = 0;

    while ((action = browser_ui_take_action(url, sizeof(url), &index)) != BROWSER_UI_NONE) {
        switch (action) {
        case BROWSER_UI_NAVIGATE:
            ps5_log("shell: navigate %s\n", url);
            webkit_load_url(url);
            browser_ui_set_page(url, url);
            browser_ui_show_status("Loading...");
            break;
        case BROWSER_UI_QUIT:
            playkit_complete = true;
            break;
        case BROWSER_UI_OSK:
            ps5_log("shell: on-screen keyboard requested\n");
            browser_ui_show_status("Page keyboard arrives with WebKit");
            break;
        case BROWSER_UI_TAB_NEW:
        case BROWSER_UI_TAB_SWITCH:
        case BROWSER_UI_TAB_CLOSE:
            ps5_log("shell: tab action %d index %d\n", (int)action, index);
            browser_ui_show_status("One tab until WebKit links");
            break;
        case BROWSER_UI_BACK:
        case BROWSER_UI_FORWARD:
        case BROWSER_UI_RELOAD:
        case BROWSER_UI_STOP:
        case BROWSER_UI_ZOOM_IN:
        case BROWSER_UI_ZOOM_OUT:
            ps5_log("shell: action %d pending WebKit\n", (int)action);
            browser_ui_show_status("WebKit engine not linked yet");
            break;
        case BROWSER_UI_HOME:
        case BROWSER_UI_NONE:
        default:
            break;
        }
    }
}

static void run_loop(void)
{
    ps5_pad_state pad, prev;
    uint32_t frame_ms, last_frame_ms;
    static uint32_t page_frame[SCREEN_WIDTH * SCREEN_HEIGHT];

    memset(&pad, 0, sizeof(pad));
    memset(&prev, 0, sizeof(prev));

    browser_ui_set_page("playkit://start", "PlayKit");

    frame_ms = now_ms();
    last_frame_ms = frame_ms;

    while (!playkit_complete) {
        float seconds;
        uint32_t now;

        now = now_ms();
        seconds = (float)(now - last_frame_ms) / 1000.0f;
        if (seconds > 0.1f) {
            seconds = 0.1f;
        }
        last_frame_ms = now;

        prev = pad;
        ps5_pad_poll(&pad);

        /* Hold both sticks to exit. */
        if ((pad.held & PS5_PAD_L3) && (pad.held & PS5_PAD_R3)) {
            playkit_complete = true;
            break;
        }

        /* The shell takes the pad while its chrome, home screen or keyboard
         * has focus. Circle leaves the title only when the shell did not use
         * it. */
        if (!browser_ui_handle_pad(&pad, &prev)) {
            if (pad.pressed & PS5_PAD_CIRCLE) {
                playkit_complete = true;
                break;
            }
        }
        shell_actions();

        webkit_paint_frame(page_frame);
        browser_ui_render(page_frame, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH, seconds);

        ps5_video_present(page_frame, SCREEN_WIDTH);
    }
}

int main(void)
{
    size_t live, peak, failed;

    ps5_log("start\n");

    ps5_heap_init();
    ps5_heap_stats(&live, &peak, &failed);
    ps5_log("heap: %zu MiB mapped, %zu KiB live, %zu KiB peak, %zu failed\n",
            ps5_heap_capacity() / (1024 * 1024), live / 1024, peak / 1024, failed);

    (void)sceSystemServiceHideSplashScreen();

    if (!ps5_video_start()) {
        ps5_log("fatal: video output initialisation failed\n");
        app_exit();
    }
    ps5_log("video ok\n");

    if (!ps5_pad_start()) {
        ps5_log("fatal: controller initialisation failed\n");
        app_exit();
    }
    ps5_log("input ok\n");

    ensure_writable_state();
    ps5_log("state ok\n");

    char favicon_dir[300];

    ps5_state_path(favicon_dir, sizeof(favicon_dir), "favicons");
    if (!favicons_init(favicon_dir))
        ps5_log("favicons: worker unavailable, using fallback icons\n");

    if (!webkit_init()) {
        ps5_log("fatal: WebKit initialisation failed\n");
        app_exit();
    }
    ps5_log("webkit ok\n");

    {
        size_t i;
        bool shell_ready = false;

        for (i = 0; i < sizeof(shell_font_dirs) / sizeof(shell_font_dirs[0]); i++) {
            if (browser_ui_init(SCREEN_WIDTH, SCREEN_HEIGHT, shell_font_dirs[i])) {
                ps5_log("shell: fonts from %s\n", shell_font_dirs[i]);
                shell_ready = true;
                break;
            }
        }
        if (!shell_ready) {
            ps5_log("fatal: browser shell font load failed\n");
            app_exit();
        }
    }
    {
        static const char *const initial_tabs[] = {"New Tab"};

        browser_ui_set_tabs(1, 0, initial_tabs);
    }
    ps5_log("shell ok\n");

    ps5_log("entering run loop\n");
    run_loop();
    ps5_log("closing\n");
    browser_ui_shutdown();
    favicons_shutdown();
    webkit_shutdown();
    ps5_video_stop();

    /* Returning from main crashes this launch context; leave cleanly. */
    app_exit();
}
