/*
 * PlayKit - WebKit browser for PlayStation 5.
 * PS5 platform constants. Keep identical to tools/build.sh RESPATH/FONTPATH.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_PATHS_H
#define PS5_PATHS_H

/* Read-only resources shipped with the title */
#define PS5_RESPATH "/app0/assets/playkit:/download0/res/"

/* Fonts: DejaVu faces loaded by the FreeType font backend */
#define PS5_FONTPATH "/app0/assets/playkit/fonts:/download0/fonts"

/* Persistent writable area (cookies, storage, downloads) */
#define PS5_STATE_DIR "/download0"

#endif
