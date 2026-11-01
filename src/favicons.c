/*
 * PlayKit - site icons for the home screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "favicons.h"

#include "console_curl.h"

#include <curl/curl.h>
#include <png.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_SLOTS 16
#define MAX_ICON_BYTES (256 * 1024)
#define MAX_HOST 128

typedef struct {
    char url[512];
    char host[MAX_HOST];
    uint32_t *pixels;
    int state; /* 0 empty, 1 queued, 2 loading, 3 done, 4 failed */
} icon_slot;

typedef struct {
    unsigned char *data;
    size_t length;
    bool overflow;
} byte_buffer;

static icon_slot slots[MAX_SLOTS];
static int slot_count;
static char cache_directory[256];
static pthread_t worker;
static bool worker_started;
static bool worker_shutdown;
static pthread_mutex_t worker_lock;
static pthread_cond_t worker_signal;
static bool curl_ready;

/* ------------------------------------------------------------------ URL */

static void host_from_url(const char *url, char *host, size_t size)
{
    const char *start = url;
    const char *end;

    if (strncmp(start, "https://", 8) == 0)
        start += 8;
    else if (strncmp(start, "http://", 7) == 0)
        start += 7;

    end = start;
    while (*end != '\0' && *end != '/' && *end != ':' && *end != '?')
        end++;
    if ((size_t)(end - start) >= size)
        end = start + size - 1;

    memcpy(host, start, (size_t)(end - start));
    host[end - start] = '\0';
}

static void cache_path(const char *host, char *path, size_t size)
{
    char safe[MAX_HOST];
    size_t i;

    for (i = 0; host[i] != '\0' && i + 1 < sizeof(safe); i++) {
        char c = host[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
        safe[i] = keep ? c : '_';
    }
    safe[i] = '\0';
    snprintf(path, size, "%s/%s.rgba", cache_directory, safe);
}

/* ------------------------------------------------------------------ ICO */

static void store_rgba(uint32_t *out, int width, int height, const unsigned char *rgba)
{
    int x, y;

    for (y = 0; y < FAVICON_SIZE; y++) {
        int sy = y * height / FAVICON_SIZE;
        for (x = 0; x < FAVICON_SIZE; x++) {
            int sx = x * width / FAVICON_SIZE;
            const unsigned char *p = rgba + ((size_t)sy * width + sx) * 4;
            out[y * FAVICON_SIZE + x] =
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

    out = malloc(FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t));
    if (out != NULL)
        store_rgba(out, (int)image.width, (int)image.height, rgba);
    free(rgba);
    png_image_free(&image);
    return out;
}

static uint32_t *decode_dib(const unsigned char *data, size_t length)
{
    uint32_t header_size, width, height, bpp, compression, palette_size, offset;
    uint32_t row_stride, y, x;
    const unsigned char *palette;
    const unsigned char *pixels;
    unsigned char *rgba;
    uint32_t *out;

    if (length < 40)
        return NULL;

    header_size = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                  ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    width = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
            ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    height = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
             ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    bpp = (uint32_t)data[14] | ((uint32_t)data[15] << 8);
    compression = (uint32_t)data[16] | ((uint32_t)data[17] << 8);

    if (header_size < 40 || width == 0 || width > 256 || height < 2 ||
        compression != 0 || (bpp != 32 && bpp != 24 && bpp != 8))
        return NULL;

    height /= 2; /* the stored height covers XOR and AND masks */
    if (height == 0 || height > 256)
        return NULL;

    palette_size = (bpp == 8) ? 256 : 0;
    offset = header_size + palette_size * 4;
    if (offset >= length)
        return NULL;
    palette = data + header_size;
    pixels = data + offset;
    row_stride = ((width * bpp + 31) / 32) * 4;
    if (offset + (size_t)row_stride * height > length)
        return NULL;

    rgba = malloc((size_t)width * height * 4);
    if (rgba == NULL)
        return NULL;

    for (y = 0; y < height; y++) {
        const unsigned char *row = pixels + (size_t)(height - 1 - y) * row_stride;
        for (x = 0; x < width; x++) {
            unsigned char r, g, b, a = 255;

            if (bpp == 32) {
                b = row[x * 4 + 0];
                g = row[x * 4 + 1];
                r = row[x * 4 + 2];
                a = row[x * 4 + 3];
                if (a == 0)
                    a = 255; /* many icons leave alpha at zero */
            } else if (bpp == 24) {
                b = row[x * 3 + 0];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
            } else {
                unsigned index = row[x];
                b = palette[index * 4 + 0];
                g = palette[index * 4 + 1];
                r = palette[index * 4 + 2];
            }

            rgba[((size_t)y * width + x) * 4 + 0] = r;
            rgba[((size_t)y * width + x) * 4 + 1] = g;
            rgba[((size_t)y * width + x) * 4 + 2] = b;
            rgba[((size_t)y * width + x) * 4 + 3] = a;
        }
    }

    out = malloc(FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t));
    if (out != NULL)
        store_rgba(out, (int)width, (int)height, rgba);
    free(rgba);
    return out;
}

static uint32_t *decode_ico(const unsigned char *data, size_t length)
{
    unsigned count, i, best = 0;
    uint32_t best_area = 0;

    if (length < 6 || data[0] != 0 || data[1] != 0 || data[2] != 1 || data[3] != 0)
        return NULL;

    count = (unsigned)data[4] | ((unsigned)data[5] << 8);
    if (count == 0 || 6 + count * 16 > length)
        return NULL;

    for (i = 0; i < count; i++) {
        const unsigned char *entry = data + 6 + i * 16;
        uint32_t w = entry[0] == 0 ? 256 : entry[0];
        uint32_t h = entry[1] == 0 ? 256 : entry[1];
        uint32_t bytes = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                         ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
        uint32_t offset = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                          ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);

        if (bytes == 0 || (size_t)offset + bytes > length)
            continue;
        if (w * h > best_area) {
            best_area = w * h;
            best = i;
        }
    }
    if (best_area == 0)
        return NULL;

    {
        const unsigned char *entry = data + 6 + best * 16;
        uint32_t bytes = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                         ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
        uint32_t offset = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                          ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);
        const unsigned char *payload = data + offset;

        if (bytes >= 8 && payload[0] == 0x89 && payload[1] == 'P' &&
            payload[2] == 'N' && payload[3] == 'G')
            return decode_png(payload, bytes);
        return decode_dib(payload, bytes);
    }
}

static uint32_t *decode_icon(const unsigned char *data, size_t length)
{
    if (length >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
        data[3] == 'G')
        return decode_png(data, length);
    return decode_ico(data, length);
}

/* --------------------------------------------------------------- fetch */

static size_t write_body(void *contents, size_t size, size_t count, void *user)
{
    byte_buffer *buffer = user;
    size_t bytes = size * count;

    if (buffer->length + bytes > MAX_ICON_BYTES) {
        buffer->overflow = true;
        return 0;
    }
    memcpy(buffer->data + buffer->length, contents, bytes);
    buffer->length += bytes;
    return bytes;
}

static bool fetch_icon(const char *host, byte_buffer *buffer)
{
    static const char *schemes[] = {"https", "http"};
    size_t s;

    for (s = 0; s < sizeof(schemes) / sizeof(schemes[0]); s++) {
        CURL *easy = curl_easy_init();
        CURLcode result;
        char url[256];
        long status = 0;

        if (easy == NULL)
            return false;

        console_curl_setup(easy);
        snprintf(url, sizeof(url), "%s://%s/favicon.ico", schemes[s], host);
        buffer->length = 0;
        buffer->overflow = false;

        curl_easy_setopt(easy, CURLOPT_URL, url);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 4L);
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(easy, CURLOPT_USERAGENT, "PlayKit/0.1");
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, buffer);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

        result = curl_easy_perform(easy);
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(easy);

        if (result == CURLE_OK && status == 200 && !buffer->overflow &&
            buffer->length > 0)
            return true;
    }
    return false;
}

static uint32_t *load_cached(const char *host)
{
    char path[512];
    FILE *file;
    uint32_t *pixels;
    size_t read;

    cache_path(host, path, sizeof(path));
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;

    pixels = malloc(FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t));
    if (pixels == NULL) {
        fclose(file);
        return NULL;
    }
    read = fread(pixels, 1, FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t), file);
    fclose(file);
    if (read != FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t)) {
        free(pixels);
        return NULL;
    }
    return pixels;
}

static void save_cached(const char *host, const uint32_t *pixels)
{
    char path[512];
    FILE *file;

    cache_path(host, path, sizeof(path));
    file = fopen(path, "wb");
    if (file == NULL)
        return;
    fwrite(pixels, 1, FAVICON_SIZE * FAVICON_SIZE * sizeof(uint32_t), file);
    fclose(file);
}

static void *worker_main(void *argument)
{
    byte_buffer buffer;
    int i;

    (void)argument;
    buffer.data = malloc(MAX_ICON_BYTES);
    if (buffer.data == NULL)
        return NULL;

    /* Stay alive: process queued fetches as they arrive, then wait for more.
     * prefetch() signals worker_signal whenever a new slot is queued. */
    for (;;)
    {
        pthread_mutex_lock(&worker_lock);
        while (!worker_shutdown) {
            int pending = 0;
            for (i = 0; i < slot_count; i++)
                if (slots[i].state == 1) { pending = 1; break; }
            if (pending)
                break;
            pthread_cond_wait(&worker_signal, &worker_lock);
        }
        if (worker_shutdown) {
            pthread_mutex_unlock(&worker_lock);
            break;
        }
        pthread_mutex_unlock(&worker_lock);

        for (i = 0; i < slot_count; i++) {
            uint32_t *pixels;

            if (slots[i].state != 1)
                continue;
            slots[i].state = 2;

            pixels = load_cached(slots[i].host);
            if (pixels == NULL && fetch_icon(slots[i].host, &buffer)) {
                pixels = decode_icon(buffer.data, buffer.length);
                if (pixels != NULL)
                    save_cached(slots[i].host, pixels);
            }

            if (pixels != NULL) {
                slots[i].pixels = pixels;
                slots[i].state = 3;
            } else {
                slots[i].state = 4;
            }
        }
    }

    free(buffer.data);
    return NULL;
}

/* ---------------------------------------------------------------- API */

bool favicons_init(const char *cache_dir)
{
    if (worker_started)
        return true;
    if (cache_dir == NULL || cache_dir[0] == '\0')
        return false;

    snprintf(cache_directory, sizeof(cache_directory), "%s", cache_dir);
    mkdir(cache_directory, 0777);

    pthread_mutex_init(&worker_lock, NULL);
    pthread_cond_init(&worker_signal, NULL);

    if (!curl_ready) {
        curl_ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        if (!curl_ready) {
            pthread_cond_destroy(&worker_signal);
            pthread_mutex_destroy(&worker_lock);
            return false;
        }
    }

    if (pthread_create(&worker, NULL, worker_main, NULL) != 0) {
        pthread_cond_destroy(&worker_signal);
        pthread_mutex_destroy(&worker_lock);
        return false;
    }
    worker_started = true;
    return true;
}

void favicons_prefetch(const char *url)
{
    char host[MAX_HOST];
    int i;

    if (!worker_started || url == NULL)
        return;

    host_from_url(url, host, sizeof(host));
    if (host[0] == '\0')
        return;

    for (i = 0; i < slot_count; i++) {
        if (strcmp(slots[i].host, host) == 0)
            return;
    }
    if (slot_count >= MAX_SLOTS)
        return;

    snprintf(slots[slot_count].url, sizeof(slots[slot_count].url), "%s", url);
    snprintf(slots[slot_count].host, sizeof(slots[slot_count].host), "%s", host);
    slots[slot_count].state = 1;
    slot_count++;

    pthread_mutex_lock(&worker_lock);
    pthread_cond_signal(&worker_signal);
    pthread_mutex_unlock(&worker_lock);
}

const uint32_t *favicons_get(const char *url)
{
    char host[MAX_HOST];
    int i;

    if (url == NULL || url[0] == '\0')
        return NULL;

    host_from_url(url, host, sizeof(host));
    for (i = 0; i < slot_count; i++) {
        if (strcmp(slots[i].host, host) == 0)
            return slots[i].pixels;
    }
    return NULL;
}

void favicons_shutdown(void)
{
    if (worker_started) {
        pthread_mutex_lock(&worker_lock);
        worker_shutdown = true;
        pthread_cond_signal(&worker_signal);
        pthread_mutex_unlock(&worker_lock);
        pthread_join(worker, NULL);
        worker_started = false;
        pthread_cond_destroy(&worker_signal);
        pthread_mutex_destroy(&worker_lock);
    }
    if (curl_ready) {
        curl_global_cleanup();
        curl_ready = false;
    }
}
