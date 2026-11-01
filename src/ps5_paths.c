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

/* A folder counts as writable only if a file can actually be created there:
 * access() reports the mount's mode bits, which do not match what the title's
 * sandbox allows. */
static bool can_write(const char *dir)
{
    char probe[320];
    FILE *file;

    snprintf(probe, sizeof(probe), "%s/.playkit-write-test", dir);
    file = fopen(probe, "w");
    if (file == NULL)
        return false;
    fclose(file);
    remove(probe);
    return true;
}

const char *ps5_state_dir(void)
{
    static const char *const candidates[] = {PS5_APP_DIR, PS5_GAME_DIR, PS5_STATE_DIR_FALLBACK};
    const char *override;
    size_t i;

    if (state_dir_ready)
        return state_dir;

    override = getenv("PLAYKIT_STATE_DIR");
    if (override != NULL && override[0] != '\0') {
        snprintf(state_dir, sizeof(state_dir), "%s", override);
    } else {
        /* /app0 is the title's own folder, so writing there lands in the game
         * folder even though the app's view of /data is read-only. */
        snprintf(state_dir, sizeof(state_dir), "%s", PS5_STATE_DIR_FALLBACK);
        for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            if (can_write(candidates[i])) {
                snprintf(state_dir, sizeof(state_dir), "%s", candidates[i]);
                break;
            }
        }
    }

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
