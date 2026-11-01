/*
 * PlayKit - the browser shell: chrome, home screen, keyboard, menu.
 * Ported from PlaySurf (GPL-3.0-or-later, BlackBearReloaded).
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The engine renders the page into a surface the height of the screen minus
 * the chrome bar; this module owns the bar, the start screen, the on-screen
 * keyboard and the menu, and turns the controller into browser actions.
 * main.c composites: page first, then browser_ui_render() on top.
 */

#ifndef PLAYKIT_BROWSER_UI_H
#define PLAYKIT_BROWSER_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ps5.h"

/* Height of the top bar. The page surface is the rest of the screen. */
#define BROWSER_UI_CHROME_HEIGHT 130

typedef enum
{
    BROWSER_UI_NONE = 0,
    BROWSER_UI_BACK,
    BROWSER_UI_FORWARD,
    BROWSER_UI_RELOAD,
    BROWSER_UI_STOP,
    BROWSER_UI_HOME,
    BROWSER_UI_NAVIGATE,
    BROWSER_UI_ZOOM_IN,
    BROWSER_UI_ZOOM_OUT,
    BROWSER_UI_OSK,
    BROWSER_UI_QUIT,
    /* Tab actions carry the tab's index in browser_ui_take_action's index. */
    BROWSER_UI_TAB_NEW,
    BROWSER_UI_TAB_SWITCH,
    BROWSER_UI_TAB_CLOSE
} browser_ui_action;

/* Loads the interface fonts from font_dir (the directory holding the
 * .huifont files). The screen size is the composited frame's. */
bool browser_ui_init(int width, int height, const char *font_dir);
void browser_ui_shutdown(void);

/* What the browser currently shows. Called every frame; cheap. */
void browser_ui_set_page(const char *url, const char *title);

/* The engine reports the committed URL (redirects and reloads included)
 * when a load is really done. */
void browser_ui_page_committed(const char *url);
void browser_ui_set_nav(bool can_back, bool can_forward);

/* The start screen is PlayKit's own, not a page: while it is up the page
 * surface is not composited. */
bool browser_ui_is_home(void);
void browser_ui_go_home(void);

/* A short message in the corner ("Copied link", "Loading duckduckgo.com"). */
void browser_ui_show_status(const char *text);

/* Feeds the pad to the shell. Returns true when the shell used the input,
 * in which case the caller must not also drive the page with it. */
bool browser_ui_handle_pad(const ps5_pad_state *pad, const ps5_pad_state *prev);

/* The action the shell produced, if any; NAVIGATE fills url, the tab actions
 * fill index. */
browser_ui_action browser_ui_take_action(char *url, size_t url_size, int *index);

/* The page click the page cursor produced, if any, in composited-frame
 * coordinates. The page cursor never clicks the shell, so this is only ever
 * a webpage click. */
bool browser_ui_take_click(int *x, int *y);

/* The tabs the shell lists: their titles and which one is showing. */
void browser_ui_set_tabs(int count, int active, const char *const *titles);

/* Draws the shell over the composited frame. seconds is the frame step. */
void browser_ui_render(uint32_t *frame, int width, int height, int stride,
                       float seconds);

/* The address the address bar is showing, for the caller's own use. */
const char *browser_ui_address(void);

/* True while the stick drives the page cursor: the page owns the pointer,
 * the shell crosshair is hidden, and clicks go to the webpage. */
bool browser_ui_pointer_visible(void);

#endif /* PLAYKIT_BROWSER_UI_H */
