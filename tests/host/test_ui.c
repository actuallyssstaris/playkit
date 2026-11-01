/*
 * PlayKit - host-native test of the browser shell's pad handling.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shell has two pointers: the crosshair, which is confined to the chrome
 * while a page is up, and the page cursor, which is confined to the page and
 * never reaches a shell control. This test drives the same pad path the
 * console does and checks the handover in both directions, plus the clicks
 * each pointer is allowed to produce.
 *
 * Stick values are the normalised -1..1 axes ps5_pad_poll produces, so a full
 * push moves the pointer exactly 1600/60 px across and 1100/60 px down per
 * call, and positions can be predicted without reading shell state.
 */

#include "browser_ui.h"
#include "ps5.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 1920
#define SCREEN_H 1080
#define STEP_X (1600.0f / 60.0f)
#define STEP_Y (1100.0f / 60.0f)

static int failures;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                    #condition);                                               \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void pad_step(float dx, float dy, uint32_t pressed)
{
    ps5_pad_state pad;

    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    pad.left_x = dx;
    pad.left_y = dy;
    pad.pressed = pressed;
    browser_ui_handle_pad(&pad, &pad);
}

static void move(float dx, float dy, int steps)
{
    for (int i = 0; i < steps; i++)
        pad_step(dx, dy, 0);
}

static void push(float dx, float dy)
{
    pad_step(dx, dy, 0);
}

int main(void)
{
    char url[1024];
    int index = 0;
    int click_x = 0, click_y = 0;
    browser_ui_action action;

    if (!browser_ui_init(SCREEN_W, SCREEN_H, "assets/fonts"))
    {
        fprintf(stderr, "browser_ui_init failed; run from the repository root\n");
        return 1;
    }

    /* ------------------------------------------------------------ home */
    CHECK(browser_ui_is_home());
    CHECK(!browser_ui_pointer_visible());

    /* Crosshair travels from the centre (960, 540) to the first site tile,
     * whose centre is (337, 455) at this screen size. */
    move(-1.0f, 0.0f, (int)((960.0f - 337.0f) / STEP_X));
    move(0.0f, -1.0f, (int)((540.0f - 455.0f) / STEP_Y));
    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);

    action = browser_ui_take_action(url, sizeof(url), &index);
    CHECK(action == BROWSER_UI_NAVIGATE);
    CHECK(strncmp(url, "https://duckduckgo.com/", 23) == 0);
    CHECK(!browser_ui_take_click(&click_x, &click_y));
    CHECK(!browser_ui_is_home());

    /* ------------------------------------------------- page handover */
    /* On a page the crosshair owns the stick and stays in the chrome. */
    CHECK(!browser_ui_pointer_visible());

    /* The click that navigated left the crosshair at (347, 448), over the page
     * surface. A downward push hands the stick to the page cursor, which
     * appears just under the chrome. */
    push(0.0f, 1.0f);
    CHECK(browser_ui_pointer_visible());

    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
    CHECK(browser_ui_take_click(&click_x, &click_y));
    CHECK(click_y >= BROWSER_UI_CHROME_HEIGHT);
    CHECK(click_y <= BROWSER_UI_CHROME_HEIGHT + 20);
    CHECK(click_x > 0 && click_x < SCREEN_W);
    CHECK(!browser_ui_take_click(&click_x, &click_y));

    /* The page cursor never produces a shell action, whatever the pad does. */
    CHECK(browser_ui_take_action(url, sizeof(url), &index) == BROWSER_UI_NONE);

    /* An upward push against the top of the page hands the stick back to the
     * crosshair, which reappears at the chrome's bottom edge. */
    push(0.0f, -1.0f);
    CHECK(!browser_ui_pointer_visible());

    /* ------------------------------------------------- chrome clicks */
    /* From just above the bottom edge one downward push clamps the crosshair to
     * the edge without handing over, so a chrome click is still the shell's. */
    push(0.0f, -1.0f);
    push(0.0f, 1.0f);
    CHECK(!browser_ui_pointer_visible());
    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
    CHECK(!browser_ui_take_click(&click_x, &click_y));

    /* Back to the bottom edge: the next downward push hands over again. The
     * page cursor keeps its place, so a crosshair moved sideways in the chrome
     * does not drag the page pointer with it. */
    move(-1.0f, 0.0f, 5);
    push(0.0f, 1.0f);
    CHECK(browser_ui_pointer_visible());
    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
    CHECK(browser_ui_take_click(&click_x, &click_y));
    CHECK(click_x >= 346 && click_x <= 348);
    CHECK(click_y >= BROWSER_UI_CHROME_HEIGHT);
    CHECK(click_y <= BROWSER_UI_CHROME_HEIGHT + 20);

    /* An upward push returns the crosshair to the chrome, at the page
     * cursor's x again. */
    push(0.0f, -1.0f);
    CHECK(!browser_ui_pointer_visible());

    /* Back button centre is (42, 74) from there. can_back makes the click an
     * action. */
    browser_ui_set_nav(true, false);
    move(-1.0f, 0.0f, (int)((346.0f - 42.0f) / STEP_X) + 1);
    move(0.0f, -1.0f, (int)((129.0f - 88.0f) / STEP_Y) + 1);
    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
    action = browser_ui_take_action(url, sizeof(url), &index);
    CHECK(action == BROWSER_UI_BACK);
    CHECK(!browser_ui_take_click(&click_x, &click_y));

    /* The chrome is drawn on the home screen too, so its buttons must answer
     * the crosshair there as well. The cursor is still on the back button. */
    browser_ui_go_home();
    CHECK(browser_ui_is_home());
    browser_ui_set_nav(true, false);
    pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
    action = browser_ui_take_action(url, sizeof(url), &index);
    CHECK(action == BROWSER_UI_BACK);
    CHECK(!browser_ui_take_click(&click_x, &click_y));

    /* ---------------------------------------------------- menu clicks */
    /* The open menu answers the crosshair: hover a row, then click it. The
     * menu is 460 px wide at the right edge, rows start 48 px below its top
     * and are 56 px tall every 64 px. From the back button, aim at the
     * "Home" row (index 6). */
    {
        float menu_x = (float)SCREEN_W - 460.0f - 24.0f + 8.0f;
        float row_x = menu_x + 230.0f;
        float row_y = (float)BROWSER_UI_CHROME_HEIGHT + 14.0f + 48.0f +
                      6.0f * 64.0f + 28.0f;

        pad_step(0.0f, 0.0f, PS5_PAD_OPTIONS);
        move(1.0f, 0.0f, (int)((row_x - 42.0f) / STEP_X) + 1);
        move(0.0f, 1.0f, (int)((row_y - 74.0f) / STEP_Y) + 1);
        pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
        action = browser_ui_take_action(url, sizeof(url), &index);
        CHECK(action == BROWSER_UI_HOME);
        CHECK(!browser_ui_take_click(&click_x, &click_y));
    }

    /* Hovering moves the highlight, so D-pad Cross then runs the row under
     * the crosshair. The cursor is still on "Home" (1695, 606); aim at
     * "Zoom out" (index 9) and then move off the panel: the highlight stays,
     * and Cross still runs it. */
    {
        float row_y = (float)BROWSER_UI_CHROME_HEIGHT + 14.0f + 48.0f +
                      9.0f * 64.0f + 28.0f;

        pad_step(0.0f, 0.0f, PS5_PAD_OPTIONS);
        move(0.0f, 1.0f, (int)((row_y - 606.0f) / STEP_Y) + 1);
        move(-1.0f, 0.0f, 40); /* out of the panel, highlight stays */
        pad_step(0.0f, 0.0f, PS5_PAD_CROSS);
        action = browser_ui_take_action(url, sizeof(url), &index);
        CHECK(action == BROWSER_UI_ZOOM_OUT);
        CHECK(!browser_ui_take_click(&click_x, &click_y));
        pad_step(0.0f, 0.0f, PS5_PAD_CIRCLE);
    }

    browser_ui_shutdown();
    if (failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("browser_ui pad tests: all checks passed\n");
    return 0;
}
