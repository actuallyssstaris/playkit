/*
 * PlayKit - host stubs for the console and network parts of the shell.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tests/host/test_ui.c links the real src/ui.c and src/browser_ui.c against
 * this file, so the shell's pad handling runs on the build machine without
 * the console SDK, libcurl or a writable title folder.
 */

#include "favicons.h"
#include "ps5.h"
#include "ps5_paths.h"

#include <stdio.h>
#include <stdarg.h>

void ps5_log(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void ps5_state_path(char *out, size_t size, const char *name)
{
    snprintf(out, size, "/tmp/opencode/playkit-test-%s", name);
}

void favicons_prefetch(const char *url)
{
    (void)url;
}

const uint32_t *favicons_get(const char *url)
{
    (void)url;
    return NULL;
}

bool favicons_init(const char *cache_dir)
{
    (void)cache_dir;
    return true;
}

void favicons_shutdown(void)
{
}
