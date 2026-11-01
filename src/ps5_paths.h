/*
 * PlayKit - WebKit browser for PlayStation 5.
 * PS5 platform constants. Keep identical to tools/build.sh RESPATH/FONTPATH.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_PATHS_H
#define PS5_PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* Read-only resources shipped with the title */
#define PS5_RESPATH "/app0/assets/playkit:/download0/res/"

/* Fonts: DejaVu faces loaded by the FreeType font backend */
#define PS5_FONTPATH "/app0/assets/playkit/fonts:/download0/fonts"

/* Title identity, matching sce_sys/param.json. */
#define PS5_TITLE_ID "PPSA90002"

/* Folder the title is installed into on a dev console. It is writable, so the
 * app keeps its log, favicons, downloads and state there when it exists. */
#define PS5_GAME_DIR "/data/homebrew/" PS5_TITLE_ID

/* Writable area a retail-style install gets instead. */
#define PS5_STATE_DIR_FALLBACK "/download0"

/* Root for every file the app creates. PLAYKIT_STATE_DIR overrides it, which
 * is how host builds and tests point at a scratch folder. */
const char *ps5_state_dir(void);

/* "<state dir>/<name>", e.g. "playkit.log". */
void ps5_state_path(char *out, size_t size, const char *name);

#endif
