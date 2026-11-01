/*
 * PlayKit - PS5 DualSense input via scePad.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_PAD_H
#define PS5_PAD_H

#include "ps5.h"

bool ps5_pad_start(void);
void ps5_pad_poll(ps5_pad_state *pad);

#endif
