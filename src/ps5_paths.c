/*
 * PlayKit - writable storage root.
 *
 * Everything the app creates at run time (log, favicons, downloads, bookmarks,
 * engine state) lives in one folder. On a dev console the title runs from
 * /data/homebrew/<title id>, which is writable and easy to inspect over FTP, so
 * that folder wins when it exists. A retail-style install falls back to the
 * title's own writable area, /download0.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_paths.h"

static char state_dir[256];
static bool state_dir_ready;

const char *ps5_state_dir(void)
{
    const char *override;

    if (state_dir_ready)
        return state_dir;

    override = getenv("PLAYKIT_STATE_DIR");
    if (override != NULL && override[0] != '\0')
        snprintf(state_dir, sizeof(state_dir), "%s", override);
    else if (access(PS5_GAME_DIR, W_OK) == 0)
        snprintf(state_dir, sizeof(state_dir), "%s", PS5_GAME_DIR);
    else
        snprintf(state_dir, sizeof(state_dir), "%s", PS5_STATE_DIR_FALLBACK);

    mkdir(state_dir, 0777);
    state_dir_ready = true;
    return state_dir;
}

void ps5_state_path(char *out, size_t size, const char *name)
{
    if (out == NULL || size == 0)
        return;

    snprintf(out, size, "%s/%s", ps5_state_dir(), name);
}
