/*
 * PlayKit - a small immediate-mode drawing kit for the browser chrome.
 * Ported from PlaySurf (GPL-3.0-or-later, BlackBearReloaded).
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- basics */

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static float minf(float a, float b)
{
    return a < b ? a : b;
}

static void blend(uint32_t *dst, ui_colour colour, float coverage)
{
    float alpha = (colour.a / 255.0f) * coverage;
    uint32_t under;
    float r, g, b;

    if (alpha <= 0.0f)
        return;
    if (alpha >= 1.0f)
    {
        *dst = (uint32_t)colour.r | ((uint32_t)colour.g << 8) |
               ((uint32_t)colour.b << 16) | 0xff000000u;
        return;
    }
    under = *dst;
    r = (float)(under & 0xff) + ((float)colour.r - (float)(under & 0xff)) * alpha;
    g = (float)((under >> 8) & 0xff) +
        ((float)colour.g - (float)((under >> 8) & 0xff)) * alpha;
    b = (float)((under >> 16) & 0xff) +
        ((float)colour.b - (float)((under >> 16) & 0xff)) * alpha;
    *dst = (uint32_t)(r + 0.5f) | ((uint32_t)(g + 0.5f) << 8) |
           ((uint32_t)(b + 0.5f) << 16) | 0xff000000u;
}

/* Signed distance to a rounded box: negative inside, positive outside, in
 * pixels. The 1px band across the edge becomes the anti-aliasing coverage. */
static float rounded_box_distance(float px, float py, ui_rect rect, float radius)
{
    float hw = rect.w * 0.5f;
    float hh = rect.h * 0.5f;
    float cx = rect.x + hw;
    float cy = rect.y + hh;
    float qx, qy, ax, ay, inside, outside;

    radius = clampf(radius, 0.0f, minf(hw, hh));
    qx = fabsf(px - cx) - (hw - radius);
    qy = fabsf(py - cy) - (hh - radius);
    ax = maxf(qx, 0.0f);
    ay = maxf(qy, 0.0f);
    outside = sqrtf(ax * ax + ay * ay);
    inside = minf(maxf(qx, qy), 0.0f);
    return outside + inside - radius;
}

static void paint_rect(ui_canvas *canvas, ui_rect rect, float radius,
                       ui_colour colour, float coverage_scale)
{
    int x0, y0, x1, y1, x, y;

    if (rect.w <= 0.0f || rect.h <= 0.0f)
        return;
    x0 = (int)floorf(rect.x - 1.0f);
    y0 = (int)floorf(rect.y - 1.0f);
    x1 = (int)ceilf(rect.x + rect.w + 1.0f);
    y1 = (int)ceilf(rect.y + rect.h + 1.0f);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > canvas->width)
        x1 = canvas->width;
    if (y1 > canvas->height)
        y1 = canvas->height;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;

        for (x = x0; x < x1; x++)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float d = rounded_box_distance(px, py, rect, radius);
            float coverage = clampf(0.5f - d, 0.0f, 1.0f) * coverage_scale;

            if (coverage > 0.0f)
                blend(&row[x], colour, coverage);
        }
    }
}

void ui_fill(ui_canvas *canvas, ui_colour colour)
{
    int x, y;

    for (y = 0; y < canvas->height; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;

        for (x = 0; x < canvas->width; x++)
            blend(&row[x], colour, 1.0f);
    }
}

void ui_fill_rect(ui_canvas *canvas, ui_rect rect, float radius, ui_colour colour)
{
    paint_rect(canvas, rect, radius, colour, 1.0f);
}

void ui_rect_outline(ui_canvas *canvas, ui_rect rect, float radius,
                     float thickness, ui_colour colour)
{
    /* An outline is the box minus the box inset by the stroke: the outer
     * shape's coverage fades in at the same place the inner one fades out. */
    int x0, y0, x1, y1, x, y;
    ui_rect inner;

    if (rect.w <= 0.0f || rect.h <= 0.0f || thickness <= 0.0f)
        return;
    inner = rect;
    inner.x += thickness;
    inner.y += thickness;
    inner.w -= thickness * 2.0f;
    inner.h -= thickness * 2.0f;
    radius = clampf(radius, 0.0f, minf(rect.w, rect.h) * 0.5f);

    x0 = (int)floorf(rect.x - 1.0f);
    y0 = (int)floorf(rect.y - 1.0f);
    x1 = (int)ceilf(rect.x + rect.w + 1.0f);
    y1 = (int)ceilf(rect.y + rect.h + 1.0f);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > canvas->width)
        x1 = canvas->width;
    if (y1 > canvas->height)
        y1 = canvas->height;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;

        for (x = x0; x < x1; x++)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float outer = clampf(0.5f - rounded_box_distance(px, py, rect, radius), 0.0f, 1.0f);
            float inside = 0.0f;

            if (inner.w > 0.0f && inner.h > 0.0f)
                inside = clampf(0.5f - rounded_box_distance(px, py, inner,
                                                            maxf(radius - thickness, 0.0f)),
                                0.0f, 1.0f);
            if (outer > inside)
                blend(&row[x], colour, outer - inside);
        }
    }
}

void ui_shadow(ui_canvas *canvas, ui_rect rect, float radius, float blur,
               float offset_y, ui_colour colour)
{
    int pass;
    int passes = 10;

    if (blur <= 0.0f || colour.a == 0)
        return;
    /* Expanding outlines: each pass is a slightly larger, much fainter box.
     * Ten of them overlap into a soft edge without a real blur pass. */
    for (pass = passes; pass >= 1; pass--)
    {
        float t = (float)pass / (float)passes;
        ui_rect grown = rect;
        float grow = blur * t;
        ui_colour tint = colour;

        grown.x -= grow;
        grown.y -= grow + offset_y;
        grown.w += grow * 2.0f;
        grown.h += grow * 2.0f;
        tint.a = (uint8_t)((float)colour.a * (1.0f - t) / (float)passes * 2.2f);
        ui_fill_rect(canvas, grown, radius + grow, tint);
    }
}

void ui_rect_gradient(ui_canvas *canvas, ui_rect rect, float radius,
                      ui_colour top, ui_colour bottom)
{
    int x0, y0, x1, y1, x, y;

    if (rect.w <= 0.0f || rect.h <= 0.0f)
        return;
    x0 = (int)floorf(rect.x - 1.0f);
    y0 = (int)floorf(rect.y - 1.0f);
    x1 = (int)ceilf(rect.x + rect.w + 1.0f);
    y1 = (int)ceilf(rect.y + rect.h + 1.0f);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > canvas->width)
        x1 = canvas->width;
    if (y1 > canvas->height)
        y1 = canvas->height;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        float t = clampf(((float)y + 0.5f - rect.y) / rect.h, 0.0f, 1.0f);
        ui_colour colour;

        colour.r = (uint8_t)((float)top.r + ((float)bottom.r - (float)top.r) * t);
        colour.g = (uint8_t)((float)top.g + ((float)bottom.g - (float)top.g) * t);
        colour.b = (uint8_t)((float)top.b + ((float)bottom.b - (float)top.b) * t);
        colour.a = (uint8_t)((float)top.a + ((float)bottom.a - (float)top.a) * t);
        for (x = x0; x < x1; x++)
        {
            float d = rounded_box_distance((float)x + 0.5f, (float)y + 0.5f, rect, radius);
            float coverage = clampf(0.5f - d, 0.0f, 1.0f);

            if (coverage > 0.0f)
                blend(&row[x], colour, coverage);
        }
    }
}

void ui_line(ui_canvas *canvas, float x0, float y0, float x1, float y1,
             float thickness, ui_colour colour)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    float half = thickness * 0.5f;
    float nx, ny;
    int bx0, by0, bx1, by1, x, y;

    if (length <= 0.0f)
    {
        ui_circle(canvas, x0, y0, half, colour);
        return;
    }
    nx = -dy / length * half;
    ny = dx / length * half;

    bx0 = (int)floorf(minf(minf(x0, x1) - half, minf(x0, x1) + nx) - 1.0f);
    by0 = (int)floorf(minf(minf(y0, y1) - half, minf(y0, y1) + ny) - 1.0f);
    bx1 = (int)ceilf(maxf(maxf(x0, x1) + half, maxf(x0, x1) + nx) + 1.0f);
    by1 = (int)ceilf(maxf(maxf(y0, y1) + half, maxf(y0, y1) + ny) + 1.0f);
    if (bx0 < 0)
        bx0 = 0;
    if (by0 < 0)
        by0 = 0;
    if (bx1 > canvas->width)
        bx1 = canvas->width;
    if (by1 > canvas->height)
        by1 = canvas->height;

    for (y = by0; y < by1; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;

        for (x = bx0; x < bx1; x++)
        {
            float px = (float)x + 0.5f - x0;
            float py = (float)y + 0.5f - y0;
            float t = clampf((px * dx + py * dy) / (length * length), 0.0f, 1.0f);
            float ex = px - dx * t;
            float ey = py - dy * t;
            float distance = sqrtf(ex * ex + ey * ey);

            if (distance < half + 0.5f)
                blend(&row[x], colour, clampf(half + 0.5f - distance, 0.0f, 1.0f));
        }
    }
}

void ui_circle(ui_canvas *canvas, float cx, float cy, float radius,
               ui_colour colour)
{
    ui_rect box = {cx - radius, cy - radius, radius * 2.0f, radius * 2.0f};

    ui_fill_rect(canvas, box, radius, colour);
}

void ui_circle_outline(ui_canvas *canvas, float cx, float cy, float radius,
                       float thickness, ui_colour colour)
{
    ui_rect box = {cx - radius, cy - radius, radius * 2.0f, radius * 2.0f};

    ui_rect_outline(canvas, box, radius, thickness, colour);
}

void ui_triangle(ui_canvas *canvas, float x0, float y0, float x1, float y1,
                 float x2, float y2, ui_colour colour)
{
    int min_x = (int)floorf(minf(x0, minf(x1, x2)));
    int max_x = (int)ceilf(maxf(x0, maxf(x1, x2)));
    int min_y = (int)floorf(minf(y0, minf(y1, y2)));
    int max_y = (int)ceilf(maxf(y0, maxf(y1, y2)));
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    float sign = area < 0.0f ? -1.0f : 1.0f;
    float len0 = hypotf(x1 - x0, y1 - y0);
    float len1 = hypotf(x2 - x1, y2 - y1);
    float len2 = hypotf(x0 - x2, y0 - y2);
    int x, y;

    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x > canvas->width)
        max_x = canvas->width;
    if (max_y > canvas->height)
        max_y = canvas->height;
    if (area == 0.0f || len0 == 0.0f || len1 == 0.0f || len2 == 0.0f)
        return;

    for (y = min_y; y < max_y; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;

        for (x = min_x; x < max_x; x++)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            /* Signed distance to each edge, positive inside. */
            float d0 = sign * ((x1 - x0) * (py - y0) -
                               (y1 - y0) * (px - x0)) / len0;
            float d1 = sign * ((x2 - x1) * (py - y1) -
                               (y2 - y1) * (px - x1)) / len1;
            float d2 = sign * ((x0 - x2) * (py - y2) -
                               (y0 - y2) * (px - x2)) / len2;
            float edge = minf(d0, minf(d1, d2));

            if (edge >= -1.0f)
            {
                float coverage = clampf(edge + 0.5f, 0.0f, 1.0f);

                blend(&row[x], colour, coverage);
            }
        }
    }
}

/* ----------------------------------------------------------------- icons */

static void arc(ui_canvas *canvas, float cx, float cy, float radius,
                float thickness, float from, float to, ui_colour colour)
{
    int steps = 28;
    int i;
    float px = cx + cosf(from) * radius;
    float py = cy + sinf(from) * radius;

    for (i = 1; i <= steps; i++)
    {
        float angle = from + (to - from) * (float)i / (float)steps;
        float nx = cx + cosf(angle) * radius;
        float ny = cy + sinf(angle) * radius;

        ui_line(canvas, px, py, nx, ny, thickness, colour);
        px = nx;
        py = ny;
    }
}

void ui_icon(ui_canvas *canvas, ui_icon_kind kind, ui_rect box,
             ui_colour colour)
{
    float s = minf(box.w, box.h);
    float stroke = maxf(s * 0.085f, 1.6f);
    float cx = box.x + box.w * 0.5f;
    float cy = box.y + box.h * 0.5f;
    float l = box.x + s * 0.24f;
    float r = box.x + s * 0.76f;
    float t = box.y + s * 0.24f;
    float b = box.y + s * 0.76f;

    switch (kind)
    {
    case UI_ICON_BACK:
        ui_line(canvas, r, t, l + s * 0.06f, cy, stroke, colour);
        ui_line(canvas, l + s * 0.06f, cy, r, b, stroke, colour);
        break;
    case UI_ICON_FORWARD:
        ui_line(canvas, l, t, r - s * 0.06f, cy, stroke, colour);
        ui_line(canvas, r - s * 0.06f, cy, l, b, stroke, colour);
        break;
    case UI_ICON_RELOAD:
        arc(canvas, cx, cy, s * 0.29f, stroke, -2.2f, 2.1f, colour);
        ui_triangle(canvas, cx + s * 0.13f, cy - s * 0.31f,
                    cx + s * 0.31f, cy - s * 0.19f,
                    cx + s * 0.10f, cy - s * 0.09f, colour);
        break;
    case UI_ICON_STOP:
        ui_line(canvas, l, t, r, b, stroke, colour);
        ui_line(canvas, l, b, r, t, stroke, colour);
        break;
    case UI_ICON_CLOSE:
        ui_line(canvas, l, t, r, b, stroke, colour);
        ui_line(canvas, l, b, r, t, stroke, colour);
        break;
    case UI_ICON_HOME:
        ui_line(canvas, l, cy, cx, t, stroke, colour);
        ui_line(canvas, cx, t, r, cy, stroke, colour);
        ui_line(canvas, l + s * 0.06f, cy - s * 0.02f, l + s * 0.06f, b, stroke, colour);
        ui_line(canvas, r - s * 0.06f, cy - s * 0.02f, r - s * 0.06f, b, stroke, colour);
        ui_line(canvas, l + s * 0.06f, b, r - s * 0.06f, b, stroke, colour);
        break;
    case UI_ICON_MENU:
        ui_line(canvas, l, t + s * 0.04f, r, t + s * 0.04f, stroke, colour);
        ui_line(canvas, l, cy, r, cy, stroke, colour);
        ui_line(canvas, l, b - s * 0.04f, r, b - s * 0.04f, stroke, colour);
        break;
    case UI_ICON_SEARCH:
        ui_circle_outline(canvas, cx - s * 0.05f, cy - s * 0.05f, s * 0.21f,
                          stroke, colour);
        ui_line(canvas, cx + s * 0.11f, cy + s * 0.11f, r, b, stroke, colour);
        break;
    case UI_ICON_LOCK:
        ui_rect_outline(canvas, (ui_rect){l + s * 0.04f, cy - s * 0.02f,
                                          s * 0.44f, s * 0.30f},
                        s * 0.05f, stroke, colour);
        arc(canvas, cx, cy - s * 0.06f, s * 0.15f, stroke, 3.35f, 6.07f, colour);
        break;
    case UI_ICON_GLOBE:
        ui_circle_outline(canvas, cx, cy, s * 0.28f, stroke, colour);
        ui_line(canvas, cx, cy - s * 0.28f, cx, cy + s * 0.28f, stroke * 0.8f, colour);
        ui_line(canvas, cx - s * 0.28f, cy, cx + s * 0.28f, cy, stroke * 0.8f, colour);
        arc(canvas, cx, cy, s * 0.14f, stroke * 0.7f, -1.57f, 1.57f, colour);
        break;
    case UI_ICON_KEYBOARD:
        ui_rect_outline(canvas, (ui_rect){l, cy - s * 0.18f, s * 0.52f, s * 0.36f},
                        s * 0.05f, stroke * 0.85f, colour);
        ui_circle(canvas, cx - s * 0.15f, cy - s * 0.03f, s * 0.022f, colour);
        ui_circle(canvas, cx, cy - s * 0.03f, s * 0.022f, colour);
        ui_circle(canvas, cx + s * 0.15f, cy - s * 0.03f, s * 0.022f, colour);
        ui_line(canvas, cx - s * 0.14f, cy + s * 0.09f, cx + s * 0.14f,
                cy + s * 0.09f, stroke * 0.8f, colour);
        break;
    case UI_ICON_BACKSPACE:
        ui_rect_outline(canvas, (ui_rect){l + s * 0.10f, cy - s * 0.17f,
                                          s * 0.42f, s * 0.34f},
                        s * 0.05f, stroke * 0.85f, colour);
        ui_line(canvas, cx - s * 0.03f, cy - s * 0.06f, cx + s * 0.13f, cy + s * 0.08f,
                stroke * 0.85f, colour);
        ui_line(canvas, cx - s * 0.03f, cy + s * 0.08f, cx + s * 0.13f, cy - s * 0.06f,
                stroke * 0.85f, colour);
        break;
    case UI_ICON_PLUS:
        ui_line(canvas, cx, cy - s * 0.26f, cx, cy + s * 0.26f, stroke, colour);
        ui_line(canvas, cx - s * 0.26f, cy, cx + s * 0.26f, cy, stroke, colour);
        break;
    case UI_ICON_MINUS:
        ui_line(canvas, cx - s * 0.26f, cy, cx + s * 0.26f, cy, stroke, colour);
        break;
    case UI_ICON_QUIT:
        ui_rect_outline(canvas, (ui_rect){l, t, s * 0.30f, b - t}, s * 0.04f,
                        stroke, colour);
        ui_line(canvas, cx - s * 0.04f, cy, r, cy, stroke, colour);
        ui_line(canvas, r - s * 0.10f, cy - s * 0.10f, r, cy, stroke, colour);
        ui_line(canvas, r - s * 0.10f, cy + s * 0.10f, r, cy, stroke, colour);
        break;
    case UI_ICON_STAR:
    {
        float points[10][2];
        int i;

        for (i = 0; i < 10; i++)
        {
            float angle = -1.5707963f + (float)i * 0.6283185f;
            float radius = (i % 2 == 0) ? s * 0.30f : s * 0.13f;

            points[i][0] = cx + cosf(angle) * radius;
            points[i][1] = cy + sinf(angle) * radius;
        }
        for (i = 0; i < 10; i++)
        {
            int next = (i + 1) % 10;

            ui_triangle(canvas, cx, cy, points[i][0], points[i][1],
                        points[next][0], points[next][1], colour);
        }
        break;
    }
    case UI_ICON_TABS:
        /* two overlapping cards */
        ui_rect_outline(canvas, (ui_rect){l + s * 0.10f, t + s * 0.06f,
                                          s * 0.46f, s * 0.34f},
                        s * 0.05f, stroke * 0.85f, colour);
        ui_rect_outline(canvas, (ui_rect){l + s * 0.24f, t + s * 0.22f,
                                          s * 0.46f, s * 0.34f},
                        s * 0.05f, stroke * 0.85f, colour);
        break;
    case UI_ICON_DOWNLOAD:
        /* arrow into a tray */
        ui_line(canvas, cx, t + s * 0.08f, cx, cy + s * 0.04f, stroke, colour);
        ui_line(canvas, cx - s * 0.12f, cy - s * 0.06f, cx, cy + s * 0.04f,
                stroke, colour);
        ui_line(canvas, cx + s * 0.12f, cy - s * 0.06f, cx, cy + s * 0.04f,
                stroke, colour);
        ui_line(canvas, l + s * 0.12f, b - s * 0.12f, r - s * 0.12f,
                b - s * 0.12f, stroke, colour);
        break;
    case UI_ICON_COUNT:
        break;
    }
}

void ui_button_glyph(ui_canvas *canvas, ui_rect box, ui_glyph_kind kind,
                     ui_colour disc, ui_colour ink)
{
    float s = minf(box.w, box.h);
    float cx = box.x + box.w * 0.5f;
    float cy = box.y + box.h * 0.5f;
    float r = s * 0.21f;
    float stroke = maxf(s * 0.075f, 1.5f);

    ui_circle(canvas, cx, cy, s * 0.5f, disc);
    switch (kind)
    {
    case UI_GLYPH_CROSS:
        ui_line(canvas, cx - r, cy - r, cx + r, cy + r, stroke, ink);
        ui_line(canvas, cx - r, cy + r, cx + r, cy - r, stroke, ink);
        break;
    case UI_GLYPH_CIRCLE:
        ui_circle_outline(canvas, cx, cy, r * 1.15f, stroke, ink);
        break;
    case UI_GLYPH_TRIANGLE:
        ui_triangle(canvas, cx, cy - r * 1.2f, cx + r * 1.25f, cy + r * 0.9f,
                    cx - r * 1.25f, cy + r * 0.9f, ink);
        break;
    case UI_GLYPH_SQUARE:
        ui_rect_outline(canvas, (ui_rect){cx - r * 1.1f, cy - r * 1.1f,
                                          r * 2.2f, r * 2.2f},
                        r * 0.2f, stroke, ink);
        break;
    }
}

/* ----------------------------------------------------------------- fonts */

/* The .huifont layout, little-endian; see ytps5's gfx/font_format.hpp. */
#define UI_FONT_MAGIC 0x46505a50u
#define UI_FONT_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint16_t atlas_width;
    uint16_t atlas_height;
    float pixel_size;
    float sdf_range;
    float ascent;
    float descent;
    float line_gap;
    uint32_t glyph_count;
    uint32_t kern_count;
} ui_font_header;

typedef struct
{
    uint32_t first;
    uint32_t second;
    float amount;
} ui_font_kern;

bool ui_font_load(ui_font *font, const char *path)
{
    FILE *file;
    ui_font_header header;
    long size;
    uint8_t *blob;
    size_t glyph_bytes;
    size_t atlas_bytes;

    memset(font, 0, sizeof(*font));
    file = fopen(path, "rb");
    if (file == NULL)
        return false;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }
    blob = malloc((size_t)size);
    if (blob == NULL || fread(blob, 1, (size_t)size, file) != (size_t)size)
    {
        free(blob);
        fclose(file);
        return false;
    }
    fclose(file);

    if ((size_t)size < sizeof(header))
    {
        free(blob);
        return false;
    }
    memcpy(&header, blob, sizeof(header));
    if (header.magic != UI_FONT_MAGIC || header.version != UI_FONT_VERSION)
    {
        free(blob);
        return false;
    }
    glyph_bytes = (size_t)header.glyph_count * sizeof(ui_glyph);
    atlas_bytes = (size_t)header.atlas_width * (size_t)header.atlas_height;
    if ((size_t)size < sizeof(header) + glyph_bytes +
                           (size_t)header.kern_count * sizeof(ui_font_kern) +
                           atlas_bytes)
    {
        free(blob);
        return false;
    }

    font->glyphs = malloc(glyph_bytes != 0 ? glyph_bytes : 1);
    font->atlas = malloc(atlas_bytes != 0 ? atlas_bytes : 1);
    if (font->glyphs == NULL || font->atlas == NULL)
    {
        free(blob);
        ui_font_free(font);
        return false;
    }
    memcpy(font->glyphs, blob + sizeof(header), glyph_bytes);
    memcpy(font->atlas,
           blob + sizeof(header) + glyph_bytes +
               (size_t)header.kern_count * sizeof(ui_font_kern),
           atlas_bytes);
    free(blob);

    font->glyph_count = header.glyph_count;
    font->atlas_width = header.atlas_width;
    font->atlas_height = header.atlas_height;
    font->pixel_size = header.pixel_size;
    font->sdf_range = header.sdf_range;
    font->ascent = header.ascent;
    font->descent = header.descent;
    font->line_gap = header.line_gap;
    return true;
}

void ui_font_free(ui_font *font)
{
    free(font->glyphs);
    free(font->atlas);
    memset(font, 0, sizeof(*font));
}

static const ui_glyph *find_glyph(const ui_font *font, uint32_t codepoint)
{
    uint32_t lo = 0;
    uint32_t hi = font->glyph_count;

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;

        if (font->glyphs[mid].codepoint < codepoint)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < font->glyph_count && font->glyphs[lo].codepoint == codepoint)
        return &font->glyphs[lo];
    return NULL;
}

/* UTF-8, one codepoint at a time; malformed input is skipped. */
static uint32_t next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t cp;
    int extra;
    int i;

    if (*s == 0)
        return 0;
    if (s[0] < 0x80)
    {
        *text += 1;
        return s[0];
    }
    if ((s[0] & 0xe0) == 0xc0)
    {
        cp = s[0] & 0x1f;
        extra = 1;
    }
    else if ((s[0] & 0xf0) == 0xe0)
    {
        cp = s[0] & 0x0f;
        extra = 2;
    }
    else if ((s[0] & 0xf8) == 0xf0)
    {
        cp = s[0] & 0x07;
        extra = 3;
    }
    else
    {
        *text += 1;
        return 0xfffd;
    }
    for (i = 1; i <= extra; i++)
    {
        if ((s[i] & 0xc0) != 0x80)
        {
            *text += 1;
            return 0xfffd;
        }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    *text += extra + 1;
    return cp;
}

static float glyph_advance(const ui_font *font, float size, uint32_t codepoint)
{
    const ui_glyph *glyph = find_glyph(font, codepoint);

    if (glyph == NULL)
        return size * 0.28f;
    return glyph->advance * size / font->pixel_size;
}

float ui_text_width(const ui_font *font, float size, const char *text)
{
    float width = 0.0f;

    if (font == NULL || font->glyphs == NULL || text == NULL)
        return 0.0f;
    while (*text != 0)
    {
        uint32_t cp = next_codepoint(&text);

        if (cp == 0)
            break;
        width += glyph_advance(font, size, cp);
    }
    return width;
}

/* One glyph, sampled from the SDF atlas with bilinear filtering. */
static void draw_glyph(ui_canvas *canvas, const ui_font *font, float size,
                       float pen, float baseline, ui_colour colour,
                       const ui_glyph *glyph)
{
    float scale = size / font->pixel_size;
    float left = pen + glyph->offset_x * scale;
    float top = baseline + glyph->offset_y * scale;
    float width = (float)glyph->w * scale;
    float height = (float)glyph->h * scale;
    float distance_scale = font->sdf_range / 128.0f * scale;
    int x0 = (int)floorf(left);
    int y0 = (int)floorf(top);
    int x1 = (int)ceilf(left + width);
    int y1 = (int)ceilf(top + height);
    int x, y;

    if (width <= 0.0f || height <= 0.0f)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > canvas->width)
        x1 = canvas->width;
    if (y1 > canvas->height)
        y1 = canvas->height;

    for (y = y0; y < y1; y++)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        float v = ((float)y + 0.5f - top) / height * (float)glyph->h;

        for (x = x0; x < x1; x++)
        {
            float u = ((float)x + 0.5f - left) / width * (float)glyph->w;
            float ax = u + (float)glyph->x;
            float ay = v + (float)glyph->y;
            int ix = (int)ax;
            int iy = (int)ay;
            float fx = ax - (float)ix;
            float fy = ay - (float)iy;
            float s00, s10, s01, s11, s;
            float coverage;

            if (ix < 0 || iy < 0 || ix >= font->atlas_width - 1 ||
                iy >= font->atlas_height - 1)
                continue;
            s00 = font->atlas[(size_t)iy * (size_t)font->atlas_width + (size_t)ix];
            s10 = font->atlas[(size_t)iy * (size_t)font->atlas_width + (size_t)ix + 1];
            s01 = font->atlas[(size_t)(iy + 1) * (size_t)font->atlas_width + (size_t)ix];
            s11 = font->atlas[(size_t)(iy + 1) * (size_t)font->atlas_width + (size_t)ix + 1];
            s = (s00 * (1.0f - fx) + s10 * fx) * (1.0f - fy) +
                (s01 * (1.0f - fx) + s11 * fx) * fy;
            coverage = clampf(0.5f + (s - 128.0f) * distance_scale, 0.0f, 1.0f);
            if (coverage > 0.0f)
                blend(&row[x], colour, coverage);
        }
    }
}

float ui_text_draw(ui_canvas *canvas, const ui_font *font, float size, float x,
                   float baseline, ui_colour colour, const char *text)
{
    float pen = x;

    if (font == NULL || font->glyphs == NULL || text == NULL)
        return x;
    while (*text != 0)
    {
        uint32_t cp = next_codepoint(&text);
        const ui_glyph *glyph;

        if (cp == 0)
            break;
        glyph = find_glyph(font, cp);
        if (glyph == NULL)
        {
            pen += size * 0.28f;
            continue;
        }
        draw_glyph(canvas, font, size, pen, baseline, colour, glyph);
        pen += glyph->advance * size / font->pixel_size;
    }
    return pen;
}

float ui_text_draw_elided(ui_canvas *canvas, const ui_font *font, float size,
                          float x, float baseline, ui_colour colour,
                          const char *text, float max_width)
{
    const char *walk;
    const char *cut;
    char buffer[1024];
    float ellipsis = ui_text_width(font, size, "\xe2\x80\xa6");
    float width = 0.0f;

    if (text == NULL)
        return x;
    if (ui_text_width(font, size, text) <= max_width)
        return ui_text_draw(canvas, font, size, x, baseline, colour, text);

    /* Walk the string while it still fits next to the ellipsis, then cut. */
    cut = text;
    walk = text;
    while (*walk != 0)
    {
        const char *probe = walk;
        uint32_t cp = next_codepoint(&probe);
        float advance = glyph_advance(font, size, cp);

        if (width + advance + ellipsis > max_width)
            break;
        width += advance;
        cut = probe;
        walk = probe;
        if ((size_t)(cut - text) + 4 >= sizeof(buffer))
            break;
    }
    {
        size_t length = (size_t)(cut - text);

        memcpy(buffer, text, length);
        memcpy(buffer + length, "\xe2\x80\xa6", 4); /* U+2026 */
        buffer[length + 4] = '\0';
        return ui_text_draw(canvas, font, size, x, baseline, colour, buffer);
    }
}

const ui_theme ui_light = {
    .page = {246, 246, 247, 255},
    .surface = {232, 232, 234, 255},
    .surface_high = {214, 214, 218, 255},
    .text = {26, 27, 30, 255},
    .text_muted = {92, 95, 102, 255},
    .primary = {34, 102, 214, 255},
    .on_primary = {255, 255, 255, 255},
    .outline = {198, 200, 206, 255},
    .focus = {20, 20, 22, 255},
    .shadow = {0, 0, 0, 46},
    .accent = {34, 102, 214, 255},
    .success = {22, 128, 78, 255},
    .warning = {176, 106, 6, 255},
    .radius = 12.0f,
    .radius_card = 20.0f,
};

const ui_theme ui_dark = {
    .page = {15, 15, 15, 255},
    .surface = {38, 38, 38, 255},
    .surface_high = {58, 58, 58, 255},
    .text = {241, 241, 241, 255},
    .text_muted = {170, 170, 170, 255},
    .primary = {74, 144, 226, 255},
    .on_primary = {255, 255, 255, 255},
    .outline = {63, 63, 63, 255},
    .focus = {255, 255, 255, 255},
    .shadow = {0, 0, 0, 160},
    .accent = {74, 144, 226, 255},
    .success = {48, 164, 108, 255},
    .warning = {245, 165, 36, 255},
    .radius = 12.0f,
    .radius_card = 20.0f,
};
