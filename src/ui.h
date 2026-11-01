/*
 * PlayKit - a small immediate-mode drawing kit for the browser chrome.
 * Ported from PlaySurf (GPL-3.0-or-later, BlackBearReloaded).
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console frontend draws its own GUI over the page surface. There
 * is no OpenGL on this path (the page arrives as a linear RGBA buffer and
 * goes out through VideoOut), so this kit paints into the same 32bpp frame
 * with rounded boxes, soft shadows and SDF text, in the design language of
 * ps5-homebrew-ui (the kit ytps5 uses).
 *
 * Everything is immediate mode: no retained widget tree, no allocation per
 * frame. Colours are straight alpha, pixels are 0xAABBGGRR (RGBA8 in
 * memory), which is what VideoOut and the page surface both use.
 */

#ifndef PLAYKIT_UI_H
#define PLAYKIT_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t r, g, b, a;
} ui_colour;

typedef struct
{
    float x, y, w, h;
} ui_rect;

typedef struct
{
    uint32_t *pixels;
    int width;
    int height;
    int stride; /* in pixels */
} ui_canvas;

/* ---------------------------------------------------------------- colour */

static inline ui_colour ui_rgb(uint32_t rgb)
{
    ui_colour c = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, 255};
    return c;
}

static inline ui_colour ui_rgba(uint32_t rgb, float alpha)
{
    ui_colour c = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb,
                   (uint8_t)(alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 255.0f : alpha * 255.0f))};
    return c;
}

static inline ui_colour ui_with_alpha(ui_colour c, float alpha)
{
    c.a = (uint8_t)(alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 255.0f : alpha * 255.0f));
    return c;
}

/* ----------------------------------------------------------------- fonts */

/* A glyph rectangle in the baked atlas, plus its placement on the baseline. */
typedef struct
{
    uint32_t codepoint;
    uint16_t x, y, w, h;
    float offset_x, offset_y;
    float advance;
} ui_glyph;

/* One .huifont: a single-channel signed-distance atlas (see the format in
 * ytps5's gfx/font_format.hpp, written by tools/font-baker). */
typedef struct
{
    ui_glyph *glyphs;
    uint32_t glyph_count;
    uint8_t *atlas;
    int atlas_width, atlas_height;
    float pixel_size;
    float sdf_range;
    float ascent, descent, line_gap;
} ui_font;

bool ui_font_load(ui_font *font, const char *path);
void ui_font_free(ui_font *font);

/* Width of the text at this pixel size, without kerning (the baked faces
 * carry no kerning pairs worth the lookup). */
float ui_text_width(const ui_font *font, float size, const char *text);

/* Draws on the baseline and returns the pen position after the text. */
float ui_text_draw(ui_canvas *canvas, const ui_font *font, float size, float x,
                   float baseline, ui_colour colour, const char *text);

/* Same, cut to max_width with an ellipsis. */
float ui_text_draw_elided(ui_canvas *canvas, const ui_font *font, float size,
                          float x, float baseline, ui_colour colour,
                          const char *text, float max_width);

/* --------------------------------------------------------------- drawing */

void ui_fill(ui_canvas *canvas, ui_colour colour);

/* A rounded box, anti-aliased from its signed distance. radius is clamped to
 * half the shorter side, so 1000 means "pill". */
void ui_fill_rect(ui_canvas *canvas, ui_rect rect, float radius, ui_colour colour);
void ui_rect_outline(ui_canvas *canvas, ui_rect rect, float radius,
                     float thickness, ui_colour colour);

/* A soft drop shadow under the box: a few expanding outlines, which is cheap
 * and reads the same at this size. */
void ui_shadow(ui_canvas *canvas, ui_rect rect, float radius, float blur,
               float offset_y, ui_colour colour);

/* Vertical gradient inside a rounded box. */
void ui_rect_gradient(ui_canvas *canvas, ui_rect rect, float radius,
                      ui_colour top, ui_colour bottom);

void ui_line(ui_canvas *canvas, float x0, float y0, float x1, float y1,
             float thickness, ui_colour colour);
void ui_circle(ui_canvas *canvas, float cx, float cy, float radius,
               ui_colour colour);
void ui_circle_outline(ui_canvas *canvas, float cx, float cy, float radius,
                       float thickness, ui_colour colour);
void ui_triangle(ui_canvas *canvas, float x0, float y0, float x1, float y1,
                 float x2, float y2, ui_colour colour);

/* ---------------------------------------------------------------- icons */

typedef enum
{
    UI_ICON_BACK,
    UI_ICON_FORWARD,
    UI_ICON_RELOAD,
    UI_ICON_STOP,
    UI_ICON_HOME,
    UI_ICON_MENU,
    UI_ICON_SEARCH,
    UI_ICON_LOCK,
    UI_ICON_GLOBE,
    UI_ICON_CLOSE,
    UI_ICON_KEYBOARD,
    UI_ICON_BACKSPACE,
    UI_ICON_PLUS,
    UI_ICON_MINUS,
    UI_ICON_QUIT,
    UI_ICON_STAR,
    UI_ICON_TABS,
    UI_ICON_DOWNLOAD,
    UI_ICON_COUNT
} ui_icon_kind;

/* Draws the icon centred in box, with a stroke proportional to the box. */
void ui_icon(ui_canvas *canvas, ui_icon_kind kind, ui_rect box,
             ui_colour colour);

/* ---------------------------------------------------------------- theme */

typedef struct
{
    ui_colour page;
    ui_colour surface;
    ui_colour surface_high;
    ui_colour text;
    ui_colour text_muted;
    ui_colour primary;
    ui_colour on_primary;
    ui_colour outline;
    ui_colour focus;
    ui_colour shadow;
    ui_colour accent;
    ui_colour success;
    ui_colour warning;
    float radius;
    float radius_card;
} ui_theme;

/* PlayKit's palette: the dark grey of ps5-homebrew-ui's SmartTube theme,
 * with a blue accent so it does not read as YouTube. */
extern const ui_theme ui_dark;
extern const ui_theme ui_light;

/* The four face buttons, drawn as the shapes they are so the hint rows need
 * no font of their own. */
typedef enum
{
    UI_GLYPH_CROSS,
    UI_GLYPH_CIRCLE,
    UI_GLYPH_TRIANGLE,
    UI_GLYPH_SQUARE
} ui_glyph_kind;

/* A filled disc with the button's symbol, as the hint rows show it. */
void ui_button_glyph(ui_canvas *canvas, ui_rect box, ui_glyph_kind kind,
                     ui_colour disc, ui_colour ink);

#endif /* PLAYKIT_UI_H */
