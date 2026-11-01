/*
 * PlayKit - site icons for the home screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "favicons.h"

#include "console_curl.h"
#include "icon_decode.h"
#include "ps5.h"

#include <curl/curl.h>
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
    int attempts;
} icon_slot;

typedef struct {
    unsigned char *data;
    size_t length;
    bool overflow;
    bool truncate;
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

/* --------------------------------------------------------------- fetch */

static size_t write_body(void *contents, size_t size, size_t count, void *user)
{
    byte_buffer *buffer = user;
    size_t bytes = size * count;

    if (buffer->length + bytes > MAX_ICON_BYTES) {
        buffer->overflow = true;
        if (!buffer->truncate)
            return 0;
        /* For HTML discovery the first bytes hold <head>, so keep them and
         * swallow the rest instead of failing the transfer. */
        bytes = MAX_ICON_BYTES - buffer->length;
        memcpy(buffer->data + buffer->length, contents, bytes);
        buffer->length = MAX_ICON_BYTES;
        return size * count;
    }
    memcpy(buffer->data + buffer->length, contents, bytes);
    buffer->length += bytes;
    return bytes;
}

static bool fetch_url(const char *url, byte_buffer *buffer, bool truncate)
{
    CURL *easy = curl_easy_init();
    CURLcode result;
    long status = 0;

    if (easy == NULL)
        return false;

    console_curl_setup(easy);
    buffer->length = 0;
    buffer->overflow = false;
    buffer->truncate = truncate;

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (PlayStation 5) PlayKit/0.1");
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, buffer);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    result = curl_easy_perform(easy);
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(easy);

    buffer->data[buffer->length] = '\0';
    return result == CURLE_OK && status == 200 && buffer->length > 0;
}

static bool looks_like_icon(const byte_buffer *buffer)
{
    if (buffer->length >= 8 && buffer->data[0] == 0x89 &&
        buffer->data[1] == 'P' && buffer->data[2] == 'N' &&
        buffer->data[3] == 'G')
        return true;
    if (buffer->length >= 6 && buffer->data[0] == 0 && buffer->data[1] == 0 &&
        buffer->data[2] == 1 && buffer->data[3] == 0)
        return true;
    return false;
}

static char lower_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static const char *ci_find(const char *haystack, const char *needle)
{
    size_t length = strlen(needle);
    const char *p;

    for (p = haystack; *p != '\0'; p++) {
        size_t i;

        for (i = 0; i < length; i++) {
            if (lower_char(p[i]) != lower_char(needle[i]))
                break;
        }
        if (i == length)
            return p;
    }
    return NULL;
}

static void resolve_url(const char *scheme, const char *host, const char *href,
                        char *out, size_t size)
{
    if (strncmp(href, "https://", 8) == 0 || strncmp(href, "http://", 7) == 0)
        snprintf(out, size, "%s", href);
    else if (href[0] == '/' && href[1] == '/')
        snprintf(out, size, "%s:%s", scheme, href);
    else if (href[0] == '/')
        snprintf(out, size, "%s://%s%s", scheme, host, href);
    else
        snprintf(out, size, "%s://%s/%s", scheme, host, href);
}

/* Pull the first <link ... icon ... href="..."> out of a page head. This is
 * how sites that do not serve /favicon.ico advertise their icon. */
static bool extract_icon_url(const char *html, const char *scheme,
                             const char *host, char *out, size_t size)
{
    const char *p = html;

    while ((p = ci_find(p, "<link")) != NULL) {
        const char *end;
        const char *href;
        const char *value;
        const char *scan;
        char tag[2048];
        char url[512];
        char quote;
        size_t length;

        p += 5;
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '>')
            continue;

        end = strchr(p, '>');
        if (end == NULL)
            break;

        length = (size_t)(end - p);
        if (length >= sizeof(tag))
            length = sizeof(tag) - 1;
        memcpy(tag, p, length);
        tag[length] = '\0';

        if (ci_find(tag, "icon") == NULL ||
            (href = ci_find(tag, "href")) == NULL) {
            p = end + 1;
            continue;
        }

        scan = href + 4;
        while (*scan == ' ' || *scan == '\t' || *scan == '\n' || *scan == '\r')
            scan++;
        if (*scan != '=') {
            p = end + 1;
            continue;
        }
        scan++;
        while (*scan == ' ' || *scan == '\t' || *scan == '\n' || *scan == '\r')
            scan++;

        quote = 0;
        if (*scan == '"' || *scan == '\'')
            quote = *scan++;
        value = scan;
        while (*scan != '\0' && *scan != '>' &&
               (quote != 0 ? *scan != quote
                           : (*scan != ' ' && *scan != '\t' &&
                              *scan != '\n' && *scan != '\r')))
            scan++;

        length = (size_t)(scan - value);
        if (length > 0 && length < sizeof(url) &&
            strncmp(value, "data:", 5) != 0) {
            memcpy(url, value, length);
            url[length] = '\0';
            resolve_url(scheme, host, url, out, size);
            return true;
        }

        p = end + 1;
    }
    return false;
}

static bool fetch_icon(const char *host, byte_buffer *buffer)
{
    static const char *schemes[] = {"https", "http"};
    char url[512];
    size_t s;

    /* The classic location first: most sites still serve it. */
    for (s = 0; s < sizeof(schemes) / sizeof(schemes[0]); s++) {
        snprintf(url, sizeof(url), "%s://%s/favicon.ico", schemes[s], host);
        if (fetch_url(url, buffer, false) && looks_like_icon(buffer))
            return true;
    }

    /* Then read the page head for a <link rel="icon">. YouTube and other
     * sites only advertise their icon there. */
    for (s = 0; s < sizeof(schemes) / sizeof(schemes[0]); s++) {
        char icon[512];

        snprintf(url, sizeof(url), "%s://%s/", schemes[s], host);
        if (!fetch_url(url, buffer, true))
            continue;
        if (!extract_icon_url((const char *)buffer->data, schemes[s], host,
                              icon, sizeof(icon)))
            continue;
        if (fetch_url(icon, buffer, false) && looks_like_icon(buffer))
            return true;
    }

    /* Last resort: the touch icon Apple clients use. */
    for (s = 0; s < sizeof(schemes) / sizeof(schemes[0]); s++) {
        snprintf(url, sizeof(url), "%s://%s/apple-touch-icon.png", schemes[s],
                 host);
        if (fetch_url(url, buffer, false) && looks_like_icon(buffer))
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
    buffer.data = malloc(MAX_ICON_BYTES + 1);
    if (buffer.data == NULL)
        return NULL;
    buffer.length = 0;
    buffer.overflow = false;
    buffer.truncate = false;

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
            bool fetched = false;

            if (slots[i].state != 1)
                continue;
            slots[i].state = 2;

            pixels = load_cached(slots[i].host);
            if (pixels == NULL) {
                fetched = fetch_icon(slots[i].host, &buffer);
                if (fetched) {
                    pixels = playkit_icon_decode(buffer.data, buffer.length);
                    if (pixels != NULL)
                        save_cached(slots[i].host, pixels);
                }
            }

            if (pixels != NULL) {
                slots[i].pixels = pixels;
                slots[i].state = 3;
                ps5_log("favicon: %s ready\n", slots[i].host);
            } else if (slots[i].attempts < 1) {
                /* One retry covers a flaky first request. */
                slots[i].attempts++;
                slots[i].state = 1;
                ps5_log("favicon: %s retry\n", slots[i].host);
            } else {
                slots[i].state = 4;
                ps5_log("favicon: %s %s\n", slots[i].host,
                        fetched ? "decode failed" : "fetch failed");
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
    slots[slot_count].attempts = 0;
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
