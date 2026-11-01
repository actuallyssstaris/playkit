/*
 * PlayKit - icon decoding for site favicons.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "icon_decode.h"

#include <png.h>
#include <stdlib.h>
#include <string.h>

#define ICON_SIZE PLAYKIT_ICON_SIZE

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_u16(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void store_rgba(uint32_t *out, int width, int height,
                       const unsigned char *rgba)
{
    int x, y;

    for (y = 0; y < ICON_SIZE; y++) {
        int sy = y * height / ICON_SIZE;
        for (x = 0; x < ICON_SIZE; x++) {
            int sx = x * width / ICON_SIZE;
            const unsigned char *p = rgba + ((size_t)sy * width + sx) * 4;
            out[y * ICON_SIZE + x] =
                ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
                ((uint32_t)p[1] << 8) | p[0];
        }
    }
}

static uint32_t *decode_png(const unsigned char *data, size_t length)
{
    png_image image;
    unsigned char *rgba;
    uint32_t *out;

    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, length))
        return NULL;

    image.format = PNG_FORMAT_RGBA;
    rgba = malloc(PNG_IMAGE_SIZE(image));
    if (rgba == NULL) {
        png_image_free(&image);
        return NULL;
    }
    if (!png_image_finish_read(&image, NULL, rgba, 0, NULL)) {
        free(rgba);
        png_image_free(&image);
        return NULL;
    }

    out = malloc(ICON_SIZE * ICON_SIZE * sizeof(uint32_t));
    if (out != NULL)
        store_rgba(out, (int)image.width, (int)image.height, rgba);
    free(rgba);
    png_image_free(&image);
    return out;
}

static uint32_t *decode_dib(const unsigned char *data, size_t length)
{
    uint32_t header_size, width, height, bpp, compression, colours, offset;
    uint32_t row_stride, mask_stride;
    const unsigned char *palette;
    const unsigned char *pixels;
    const unsigned char *mask = NULL;
    unsigned char *rgba;
    uint32_t *out;
    uint32_t x, y;

    if (length < 40)
        return NULL;

    header_size = read_u32(data);
    width = read_u32(data + 4);
    height = read_u32(data + 8);
    bpp = read_u16(data + 14);
    compression = read_u32(data + 16);

    if (header_size < 40 || width == 0 || width > 256 || height < 2)
        return NULL;
    height /= 2; /* the stored height covers XOR and AND masks */
    if (height == 0 || height > 256)
        return NULL;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 &&
        bpp != 32)
        return NULL;
    if (compression != 0 && compression != 3)
        return NULL;
    if (compression == 3 && bpp != 16 && bpp != 32)
        return NULL;

    colours = 0;
    if (bpp <= 8) {
        colours = read_u32(data + 32); /* biClrUsed */
        if (colours == 0)
            colours = 1u << bpp;
        if (colours > 256)
            return NULL;
    }

    offset = header_size + colours * 4;
    row_stride = ((width * bpp + 31) / 32) * 4;
    if (offset + (size_t)row_stride * height > length)
        return NULL;

    palette = data + header_size;
    pixels = data + offset;

    /* The 1-bit AND mask follows the colour data and marks transparent
     * pixels. Some 32 bpp icons put real alpha in the pixels instead. */
    mask_stride = ((width + 31) / 32) * 4;
    if (offset + (size_t)row_stride * height +
            (size_t)mask_stride * height <= length)
        mask = data + offset + (size_t)row_stride * height;

    rgba = malloc((size_t)width * height * 4);
    if (rgba == NULL)
        return NULL;

    for (y = 0; y < height; y++) {
        const unsigned char *row =
            pixels + (size_t)(height - 1 - y) * row_stride;
        const unsigned char *mask_row =
            mask != NULL ? mask + (size_t)(height - 1 - y) * mask_stride : NULL;

        for (x = 0; x < width; x++) {
            unsigned char r = 0, g = 0, b = 0, a = 255;

            if (bpp == 32) {
                b = row[x * 4 + 0];
                g = row[x * 4 + 1];
                r = row[x * 4 + 2];
                a = row[x * 4 + 3];
                if (a == 0 && mask == NULL)
                    a = 255; /* legacy icons leave alpha at zero */
            } else if (bpp == 24) {
                b = row[x * 3 + 0];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
            } else if (bpp == 16) {
                uint32_t value = read_u16(row + x * 2);
                r = (unsigned char)(((value >> 10) & 31) * 255 / 31);
                g = (unsigned char)(((value >> 5) & 31) * 255 / 31);
                b = (unsigned char)((value & 31) * 255 / 31);
            } else {
                unsigned index;

                if (bpp == 8)
                    index = row[x];
                else if (bpp == 4)
                    index = (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
                else
                    index = (row[x / 8] >> (7 - (x & 7))) & 1;

                if (index < colours) {
                    b = palette[index * 4 + 0];
                    g = palette[index * 4 + 1];
                    r = palette[index * 4 + 2];
                }
            }

            if (mask_row != NULL &&
                (mask_row[x / 8] & (0x80u >> (x & 7))) != 0)
                a = 0;

            rgba[((size_t)y * width + x) * 4 + 0] = r;
            rgba[((size_t)y * width + x) * 4 + 1] = g;
            rgba[((size_t)y * width + x) * 4 + 2] = b;
            rgba[((size_t)y * width + x) * 4 + 3] = a;
        }
    }

    out = malloc(ICON_SIZE * ICON_SIZE * sizeof(uint32_t));
    if (out != NULL)
        store_rgba(out, (int)width, (int)height, rgba);
    free(rgba);
    return out;
}

static uint32_t *decode_ico_entry(const unsigned char *data, size_t length,
                                  const unsigned char *entry)
{
    uint32_t bytes = read_u32(entry + 8);
    uint32_t offset = read_u32(entry + 12);
    const unsigned char *payload;

    if (bytes < 8 || (size_t)offset + bytes > length)
        return NULL;
    payload = data + offset;

    if (payload[0] == 0x89 && payload[1] == 'P' && payload[2] == 'N' &&
        payload[3] == 'G')
        return decode_png(payload, bytes);
    return decode_dib(payload, bytes);
}

static uint32_t *decode_ico(const unsigned char *data, size_t length)
{
    unsigned count, i;
    unsigned order[64];
    unsigned order_count;

    if (length < 6 || data[0] != 0 || data[1] != 0 || data[2] != 1 ||
        data[3] != 0)
        return NULL;

    count = (unsigned)data[4] | ((unsigned)data[5] << 8);
    if (count == 0 || 6 + (size_t)count * 16 > length)
        return NULL;

    order_count = count < 64 ? count : 64;
    for (i = 0; i < order_count; i++)
        order[i] = i;

    /* Largest entry first: it scales down better than a 16x16 one. */
    for (i = 1; i < order_count; i++) {
        unsigned key = order[i];
        unsigned j = i;

        while (j > 0) {
            const unsigned char *a = data + 6 + order[j - 1] * 16;
            const unsigned char *b = data + 6 + key * 16;
            uint32_t area_a = (uint32_t)(a[0] ? a[0] : 256) *
                              (uint32_t)(a[1] ? a[1] : 256);
            uint32_t area_b = (uint32_t)(b[0] ? b[0] : 256) *
                              (uint32_t)(b[1] ? b[1] : 256);

            if (area_a >= area_b)
                break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }

    for (i = 0; i < order_count; i++) {
        uint32_t *pixels =
            decode_ico_entry(data, length, data + 6 + order[i] * 16);
        if (pixels != NULL)
            return pixels;
    }
    return NULL;
}

uint32_t *playkit_icon_decode(const unsigned char *data, size_t length)
{
    if (data == NULL || length < 8)
        return NULL;
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return decode_png(data, length);
    return decode_ico(data, length);
}
