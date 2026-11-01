/*
 * PlayKit - the browser shell: chrome, home screen, keyboard, menu.
 * Ported from PlaySurf (GPL-3.0-or-later, BlackBearReloaded).
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The visual language is ps5-homebrew-ui's (the kit ytps5 uses): a dark grey
 * page, soft panels, one accent, focus rings instead of a pointer, and a
 * controller hint row. It is painted in software because the page itself
 * arrives as a linear RGBA buffer on this frontend.
 */

#include "browser_ui.h"

#include "favicons.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define UI_MAX_URL 1024
#define UI_MAX_TEXT 512
#define UI_MAX_KEYS 48
#define UI_MAX_TABS 8
#define UI_MAX_BOOKMARKS 64
#define UI_MAX_DOWNLOADS 32
#define UI_MAX_NAME 128

/* Where the shell keeps its own state. The console's writable area is
 * /download0 (main.c makes it); a host build falls back to the working
 * directory, and the tests point the environment at a scratch file. */
#define UI_STATE_DIR "/download0"
#define UI_DOWNLOAD_DIR UI_STATE_DIR "/downloads"

typedef enum
{
    SCREEN_PAGE,
    SCREEN_HOME
} screen_kind;

typedef enum
{
    FOCUS_PAGE,
    FOCUS_CHROME,
    FOCUS_KEYBOARD,
    FOCUS_MENU,
    FOCUS_TABS,
    FOCUS_BOOKMARKS,
    FOCUS_DOWNLOADS
} focus_kind;

typedef enum
{
    KEY_CHAR,
    KEY_SPACE,
    KEY_BACKSPACE,
    KEY_CANCEL,
    KEY_GO
} key_kind;

typedef struct
{
    const char *label;
    key_kind kind;
    char text;
    int row;
    float width; /* in key units */
} ui_key;

typedef struct
{
    const char *label;
    const char *hint;
    const char *url;
    uint32_t tint;
} quick_link;

/* The bar's controls, left to right. */
enum
{
    CHROME_BACK,
    CHROME_FORWARD,
    CHROME_RELOAD,
    CHROME_ADDRESS,
    CHROME_MENU,
    CHROME_COUNT
};

/* The menu's rows. */
enum
{
    MENU_TABS,
    MENU_BOOKMARKS,
    MENU_DOWNLOADS,
    MENU_ADD_BOOKMARK,
    MENU_ADDRESS,
    MENU_KEYBOARD,
    MENU_HOME,
    MENU_RELOAD,
    MENU_ZOOM_IN,
    MENU_ZOOM_OUT,
    MENU_THEME,
    MENU_GROUP,
    MENU_QUIT,
    MENU_COUNT
};

typedef struct
{
    char url[UI_MAX_URL];
    char title[UI_MAX_TEXT];
} bookmark_entry;

typedef struct
{
    char name[UI_MAX_NAME];
    unsigned long long size;
} download_entry;

static const quick_link quick_links[] = {
    {"DuckDuckGo", "Search the web", "https://duckduckgo.com/", 0xde5833},
    {"Wikipedia", "The free encyclopedia", "https://en.wikipedia.org/", 0x5b8dd6},
    {"Hacker News", "What the web is reading", "https://news.ycombinator.com/", 0xff6600},
    {"GitHub", "Where the code lives", "https://github.com/", 0xd8d8d8},
    {"YouTube", "Video", "https://www.youtube.com/", 0xff0000},
    {"Reddit", "Communities", "https://www.reddit.com/", 0xff4500},
    {"MDN", "Web documentation", "https://developer.mozilla.org/", 0x8cb4ff},
    {"Stack Overflow", "Answers", "https://stackoverflow.com/", 0xf48024},
};
#define QUICK_LINK_COUNT ((int)(sizeof(quick_links) / sizeof(quick_links[0])))

static ui_key keys[UI_MAX_KEYS];
static int key_count;

static struct
{
    ui_font regular;
    ui_font medium;
    ui_font mono;
    bool ready;

    int width;
    int height;

    screen_kind screen;
    focus_kind focus;

    char url[UI_MAX_URL];
    char title[UI_MAX_TEXT];
    char address[UI_MAX_TEXT];
    char nav_target[UI_MAX_URL];
    bool can_back;
    bool can_forward;

    bool loading;
    float progress;
    float load_started;
    float load_finished;

    bool light_mode;
    bool group_enabled;
    char group_name[UI_MAX_NAME];
    uint32_t group_color;

    int chrome_focus;
    int home_focus;
    int menu_focus;
    int key_focus;
    int tab_focus;
    int bookmark_focus;
    int download_focus;
    int list_scroll; /* first visible row of a long list panel */

    /* The tabs the shell lists; the pages themselves live in main.c. */
    int tab_count;
    int tab_active;
    char tab_titles[UI_MAX_TABS][UI_MAX_TEXT];

    bookmark_entry bookmarks[UI_MAX_BOOKMARKS];
    int bookmark_count;
    char bookmark_path[UI_MAX_TEXT];

    download_entry downloads[UI_MAX_DOWNLOADS];
    int download_count;
    char download_dir[UI_MAX_TEXT];

    char input[UI_MAX_TEXT];
    bool input_for_address;
    float caret;

    char status[UI_MAX_TEXT];
    float status_until;

    char action_url[UI_MAX_URL];
    browser_ui_action action;
    int action_index;

    /* Virtual cursor driven by the left stick; the touchpad button / cross
     * clicks at the cursor position. */
    float cursor_x;
    float cursor_y;
    bool cursor_active; /* the stick has moved the cursor at least once */

    float time;
} ui;

/* ---------------------------------------------------------------- helpers */

static bool pressed(const ps5_pad_state *pad, uint32_t button);

static const ui_theme *theme(void)
{
    return ui.light_mode ? &ui_light : &ui_dark;
}

static void copy_string(char *dst, size_t size, const char *src)
{
    size_t length;

    if (size == 0)
        return;
    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }
    length = strlen(src);
    if (length >= size)
        length = size - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static float text_width(const ui_font *font, float size, const char *text)
{
    return ui_text_width(font, size, text);
}

static void text_at(ui_canvas *canvas, const ui_font *font, float size, float x,
                    float baseline, ui_colour colour, const char *text)
{
    ui_text_draw(canvas, font, size, x, baseline, colour, text);
}

static void text_centred(ui_canvas *canvas, const ui_font *font, float size,
                         float cx, float baseline, ui_colour colour,
                         const char *text)
{
    float width = text_width(font, size, text);

    ui_text_draw(canvas, font, size, cx - width * 0.5f, baseline, colour, text);
}

static void focus_ring(ui_canvas *canvas, ui_rect box, float radius)
{
    ui_rect ring = {box.x - 5.0f, box.y - 5.0f, box.w + 10.0f, box.h + 10.0f};

    ui_rect_outline(canvas, ring, radius + 5.0f, 2.5f, theme()->focus);
}

/* The bar is always the same height, so the page surface is sized for it. */
static float chrome_height(void)
{
    return (float)BROWSER_UI_CHROME_HEIGHT;
}

static void set_action(browser_ui_action action)
{
    ui.action = action;
}

/* ------------------------------------------------------------- storage */

static void resolve_state_path(char *out, size_t size, const char *env,
                               const char *fallback)
{
    const char *value = getenv(env);

    if (value != NULL && value[0] != '\0')
        copy_string(out, size, value);
    else
        copy_string(out, size, fallback);
}

/* Bookmarks are one "url<TAB>title" per line: readable from a shell, and a
 * malformed line is skipped rather than losing the rest. */
static void load_bookmarks(void)
{
    FILE *file;
    char line[UI_MAX_URL + UI_MAX_TEXT + 8];

    ui.bookmark_count = 0;
    file = fopen(ui.bookmark_path, "r");
    if (file == NULL)
        return;
    while (ui.bookmark_count < UI_MAX_BOOKMARKS &&
           fgets(line, sizeof(line), file) != NULL)
    {
        char *tab;
        bookmark_entry *entry;
        size_t length = strlen(line);

        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        tab = strchr(line, '\t');
        if (tab == NULL || tab == line)
            continue;
        *tab = '\0';
        entry = &ui.bookmarks[ui.bookmark_count];
        copy_string(entry->url, sizeof(entry->url), line);
        copy_string(entry->title, sizeof(entry->title), tab + 1);
        if (entry->title[0] == '\0')
            copy_string(entry->title, sizeof(entry->title), entry->url);
        ui.bookmark_count++;
    }
    fclose(file);
}

static void save_bookmarks(void)
{
    FILE *file = fopen(ui.bookmark_path, "w");
    int i;

    if (file == NULL)
        return;
    for (i = 0; i < ui.bookmark_count; i++)
        fprintf(file, "%s\t%s\n", ui.bookmarks[i].url, ui.bookmarks[i].title);
    fclose(file);
}

static int find_bookmark(const char *url)
{
    int i;

    if (url == NULL || url[0] == '\0')
        return -1;
    for (i = 0; i < ui.bookmark_count; i++)
    {
        if (strcmp(ui.bookmarks[i].url, url) == 0)
            return i;
    }
    return -1;
}

static void add_bookmark(void)
{
    const char *url = ui.address[0] != '\0' ? ui.address : ui.url;
    bookmark_entry *entry;

    if (url[0] == '\0' || ui.screen == SCREEN_HOME)
    {
        browser_ui_show_status("Nothing to bookmark yet");
        return;
    }
    if (find_bookmark(url) >= 0)
    {
        browser_ui_show_status("Already in bookmarks");
        return;
    }
    if (ui.bookmark_count >= UI_MAX_BOOKMARKS)
    {
        browser_ui_show_status("Bookmarks are full");
        return;
    }
    entry = &ui.bookmarks[ui.bookmark_count++];
    copy_string(entry->url, sizeof(entry->url), url);
    copy_string(entry->title, sizeof(entry->title),
                ui.title[0] != '\0' ? ui.title : url);
    save_bookmarks();
    browser_ui_show_status("Bookmark added");
}

static void remove_bookmark(int index)
{
    if (index < 0 || index >= ui.bookmark_count)
        return;
    memmove(&ui.bookmarks[index], &ui.bookmarks[index + 1],
            sizeof(ui.bookmarks[0]) * (size_t)(ui.bookmark_count - index - 1));
    ui.bookmark_count--;
    if (ui.bookmark_focus >= ui.bookmark_count && ui.bookmark_focus > 0)
        ui.bookmark_focus = ui.bookmark_count - 1;
    save_bookmarks();
}

static int compare_downloads(const void *left, const void *right)
{
    const download_entry *a = left;
    const download_entry *b = right;

    return strcmp(a->name, b->name);
}

/* The downloads folder is the source of truth: the engine writes there and the
 * shell lists what is on disk, finished or not. */
static void scan_downloads(void)
{
    DIR *dir;
    struct dirent *entry;

    ui.download_count = 0;
    dir = opendir(ui.download_dir);
    if (dir == NULL)
        return;
    while (ui.download_count < UI_MAX_DOWNLOADS &&
           (entry = readdir(dir)) != NULL)
    {
        struct stat info;
        char path[UI_MAX_TEXT];
        download_entry *item;

        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", ui.download_dir, entry->d_name);
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        item = &ui.downloads[ui.download_count++];
        copy_string(item->name, sizeof(item->name), entry->d_name);
        item->size = (unsigned long long)info.st_size;
    }
    closedir(dir);
    qsort(ui.downloads, (size_t)ui.download_count, sizeof(ui.downloads[0]),
          compare_downloads);
}

static void format_size(unsigned long long bytes, char *out, size_t size)
{
    if (bytes >= 1024ull * 1024ull)
        snprintf(out, size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ull)
        snprintf(out, size, "%.0f KB", (double)bytes / 1024.0);
    else
        snprintf(out, size, "%llu B", bytes);
}

/* --------------------------------------------------------------- actions */

static void start_loading(const char *url)
{
    ui.loading = true;
    ui.progress = 0.02f;
    ui.load_started = ui.time;
    ui.load_finished = 0.0f;
    copy_string(ui.nav_target, sizeof(ui.nav_target), url != NULL ? url : "");
}

static void navigate_to(const char *url)
{
    if (url == NULL || url[0] == '\0')
        return;
    copy_string(ui.action_url, sizeof(ui.action_url), url);
    ui.action = BROWSER_UI_NAVIGATE;
    ui.screen = SCREEN_PAGE;
    ui.focus = FOCUS_PAGE;
    start_loading(url);
    browser_ui_show_status(url);
}

static void go_home_screen(void)
{
    ui.screen = SCREEN_HOME;
    ui.focus = FOCUS_PAGE;
    ui.home_focus = 0;
    copy_string(ui.address, sizeof(ui.address), "");
}

static void open_keyboard(bool for_address, const char *seed)
{
    ui.focus = FOCUS_KEYBOARD;
    ui.input_for_address = for_address;
    copy_string(ui.input, sizeof(ui.input), seed != NULL ? seed : "");
    ui.key_focus = 0;
    ui.caret = 0.0f;
}

static void close_keyboard(void)
{
    if (ui.input_for_address)
    {
        ui.focus = FOCUS_CHROME;
        ui.chrome_focus = CHROME_ADDRESS;
    }
    else
    {
        ui.focus = ui.screen == SCREEN_HOME ? FOCUS_PAGE : FOCUS_PAGE;
    }
}

static void open_menu(void)
{
    ui.focus = FOCUS_MENU;
    ui.menu_focus = 0;
}

static void append_input(const char *text)
{
    size_t length = strlen(ui.input);
    size_t add = strlen(text);

    if (length + add >= sizeof(ui.input))
        return;
    memcpy(ui.input + length, text, add + 1);
}

static void backspace_input(void)
{
    size_t length = strlen(ui.input);

    if (length == 0)
        return;
    length--;
    while (length > 0 && ((unsigned char)ui.input[length] & 0xc0) == 0x80)
        length--;
    ui.input[length] = '\0';
}

static bool input_is_url(const char *text)
{
    const char *at;

    if (strstr(text, "://") != NULL || strncmp(text, "about:", 6) == 0)
        return true;
    if (strchr(text, ' ') != NULL)
        return false;
    at = text;
    while (*at != 0)
    {
        if (*at == '.')
            return true;
        at++;
    }
    return strncmp(text, "localhost", 9) == 0;
}

static void url_encode(const char *text, char *out, size_t size)
{
    static const char *hex = "0123456789ABCDEF";
    size_t used = 0;

    for (; *text != 0 && used + 4 < size; text++)
    {
        unsigned char c = (unsigned char)*text;

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~')
        {
            out[used++] = (char)c;
        }
        else if (c == ' ')
        {
            out[used++] = '+';
        }
        else
        {
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 0x0f];
        }
    }
    out[used] = '\0';
}

/* What the address bar's text means: a URL, or a web search. */
static void resolve_address(const char *text, char *out, size_t size)
{
    char encoded[UI_MAX_TEXT * 3];
    const char *start = text;

    while (*start == ' ')
        start++;
    if (*start == '\0')
    {
        out[0] = '\0';
        return;
    }
    if (input_is_url(start))
    {
        if (strstr(start, "://") != NULL || strncmp(start, "about:", 6) == 0)
            snprintf(out, size, "%s", start);
        else
            snprintf(out, size, "http://%s", start);
        return;
    }
    url_encode(start, encoded, sizeof(encoded));
    snprintf(out, size, "https://duckduckgo.com/?q=%s", encoded);
}

/* ---------------------------------------------------------------- layout */

static ui_rect chrome_button_box(int index)
{
    float size = 44.0f;
    float bar_top = chrome_height() - 84.0f;
    float y = bar_top + (84.0f - size) * 0.5f;
    float x;

    switch (index)
    {
    case CHROME_BACK:
        x = 20.0f;
        break;
    case CHROME_FORWARD:
        x = 20.0f + size + 10.0f;
        break;
    case CHROME_RELOAD:
        x = 20.0f + (size + 10.0f) * 2.0f;
        break;
    case CHROME_MENU:
        return (ui_rect){(float)ui.width - 20.0f - size, y, size, size};
    default:
        return (ui_rect){0, 0, 0, 0};
    }
    return (ui_rect){x, y, size, size};
}

static ui_rect address_box(void)
{
    float left = 20.0f + (44.0f + 10.0f) * 3.0f + 8.0f;
    float right = (float)ui.width - 20.0f - 44.0f - 16.0f;

    return (ui_rect){left, chrome_height() - 84.0f + 16.0f, right - left, 52.0f};
}

static ui_rect content_area(void)
{
    return (ui_rect){0.0f, chrome_height(), (float)ui.width,
                     (float)ui.height - chrome_height()};
}

static ui_rect keyboard_panel(void)
{
    float width = 1520.0f;
    float height = 470.0f;

    return (ui_rect){((float)ui.width - width) * 0.5f,
                     (float)ui.height - height - 20.0f, width, height};
}

static ui_rect menu_panel(void)
{
    float width = 460.0f;
    float row = 64.0f;
    float height = 56.0f + row * (float)MENU_COUNT + 16.0f;

    return (ui_rect){(float)ui.width - width - 24.0f, chrome_height() + 14.0f,
                     width, height};
}

/* ---------------------------------------------------------- list panels */

#define LIST_VISIBLE_ROWS 7

typedef struct
{
    const char *label;
    const char *detail;
    bool current;
} list_row;

static ui_rect list_panel(int rows)
{
    float row = 64.0f;
    float visible = (float)(rows > LIST_VISIBLE_ROWS ? LIST_VISIBLE_ROWS
                                                    : (rows < 1 ? 1 : rows));
    float height = 56.0f + row * visible + 72.0f;

    return (ui_rect){((float)ui.width - 1100.0f) * 0.5f, chrome_height() + 60.0f,
                     1100.0f, height};
}

/* Keeps the focused row inside the visible window of a long list. */
static int list_scroll_for(int focus, int count)
{
    int visible = count > LIST_VISIBLE_ROWS ? LIST_VISIBLE_ROWS : count;

    if (ui.list_scroll > focus)
        ui.list_scroll = focus;
    if (ui.list_scroll > count - visible)
        ui.list_scroll = count - visible;
    if (ui.list_scroll < 0)
        ui.list_scroll = 0;
    return ui.list_scroll;
}

static void move_list_focus(int *focus, int count, const ps5_pad_state *pad)
{
    if (count <= 0)
        return;
    if (pressed(pad, PS5_PAD_UP))
        *focus = (*focus + count - 1) % count;
    if (pressed(pad, PS5_PAD_DOWN))
        *focus = (*focus + 1) % count;
}

static void draw_list_panel(ui_canvas *canvas, const char *title,
                            const char *empty, const char *footer,
                            const list_row *rows, int count, int focus)
{
    ui_rect panel = list_panel(count);
    int scroll = list_scroll_for(focus, count);
    int visible = count > LIST_VISIBLE_ROWS ? LIST_VISIBLE_ROWS : count;
    int i;

    ui_fill_rect(canvas, (ui_rect){0, 0, (float)ui.width, (float)ui.height},
                 0.0f, ui_rgba(0x000000, 0.45f));
    ui_shadow(canvas, panel, 22.0f, 34.0f, 16.0f, theme()->shadow);
    ui_fill_rect(canvas, panel, 22.0f, ui_rgb(0x1d1d1d));
    ui_rect_outline(canvas, panel, 22.0f, 1.0f, ui_rgb(0x343434));
    text_at(canvas, &ui.regular, 20.0f, panel.x + 30.0f, panel.y + 38.0f,
            theme()->text_muted, title);

    if (count == 0)
    {
        text_at(canvas, &ui.regular, 24.0f, panel.x + 30.0f, panel.y + 116.0f,
                theme()->text_muted, empty);
    }

    for (i = 0; i < visible; i++)
    {
        int index = scroll + i;
        ui_rect row = {panel.x + 12.0f, panel.y + 56.0f + (float)i * 64.0f,
                       panel.w - 24.0f, 56.0f};
        bool focused = index == focus;
        ui_colour detail = theme()->text_muted;

        if (index >= count)
            break;
        if (focused)
            ui_fill_rect(canvas, row, 12.0f, theme()->surface_high);
        if (rows[index].current)
        {
            ui_rect bar = {row.x + 4.0f, row.y + 14.0f, 4.0f, row.h - 28.0f};

            ui_fill_rect(canvas, bar, 2.0f, theme()->accent);
            detail = theme()->accent;
        }
        ui_text_draw_elided(canvas, &ui.medium, 24.0f, row.x + 22.0f,
                            row.y + 34.0f, theme()->text, rows[index].label,
                            row.w - 44.0f);
        if (rows[index].detail != NULL && rows[index].detail[0] != '\0')
            ui_text_draw_elided(canvas, &ui.regular, 18.0f, row.x + 22.0f,
                                row.y + 52.0f, detail, rows[index].detail,
                                row.w - 44.0f);
    }

    if (count > visible)
    {
        char counter[32];

        snprintf(counter, sizeof(counter), "%d-%d of %d", scroll + 1,
                 scroll + visible, count);
        text_at(canvas, &ui.regular, 16.0f, panel.x + panel.w - 30.0f -
                                                  text_width(&ui.regular, 16.0f, counter),
                panel.y + 38.0f, theme()->text_muted, counter);
    }

    text_at(canvas, &ui.regular, 18.0f, panel.x + 30.0f,
            panel.y + panel.h - 20.0f, theme()->text_muted, footer);
}

static void draw_tabs(ui_canvas *canvas)
{
    list_row rows[UI_MAX_TABS];
    char details[UI_MAX_TABS][64];
    int i;

    for (i = 0; i < ui.tab_count; i++)
    {
        snprintf(details[i], sizeof(details[i]), "Tab %d%s", i + 1,
                 i == ui.tab_active ? " - showing" : "");
        rows[i].label = ui.tab_titles[i][0] != '\0' ? ui.tab_titles[i] : "New tab";
        rows[i].detail = details[i];
        rows[i].current = i == ui.tab_active;
    }
    draw_list_panel(canvas, "Tabs", "No tabs open",
                    "X switch   Square close   Triangle new   O back", rows,
                    ui.tab_count, ui.tab_focus);
}

static void draw_bookmarks(ui_canvas *canvas)
{
    list_row rows[UI_MAX_BOOKMARKS];
    int i;

    for (i = 0; i < ui.bookmark_count; i++)
    {
        rows[i].label = ui.bookmarks[i].title;
        rows[i].detail = ui.bookmarks[i].url;
        rows[i].current = false;
    }
    draw_list_panel(canvas, "Bookmarks",
                    "No bookmarks yet - open a page and choose Add bookmark",
                    "X open   Square remove   O back", rows, ui.bookmark_count,
                    ui.bookmark_focus);
}

static void draw_downloads(ui_canvas *canvas)
{
    list_row rows[UI_MAX_DOWNLOADS];
    char sizes[UI_MAX_DOWNLOADS][32];
    int i;

    for (i = 0; i < ui.download_count; i++)
    {
        format_size(ui.downloads[i].size, sizes[i], sizeof(sizes[i]));
        rows[i].label = ui.downloads[i].name;
        rows[i].detail = sizes[i];
        rows[i].current = false;
    }
    draw_list_panel(canvas, "Downloads",
                    "No downloads yet - file links are saved to /download0/downloads",
                    "O back", rows, ui.download_count, ui.download_focus);
}

static void layout_keys(ui_rect panel, ui_rect *out)
{
    float gap = 10.0f;
    float height = 54.0f;
    float left = panel.x + 36.0f;
    float available = panel.w - 72.0f;
    float top = panel.y + 156.0f;
    int row;

    for (row = 0; row < 4; row++)
    {
        float units = 0.0f;
        float unit;
        float x = 0.0f;
        int count = 0;
        int i;

        for (i = 0; i < key_count; i++)
        {
            if (keys[i].row != row)
                continue;
            units += keys[i].width;
            count++;
        }
        if (count == 0)
            continue;
        unit = (available - gap * (float)(count - 1)) / units;
        if (unit > 96.0f)
            unit = 96.0f;
        x = left + (available - (unit * units + gap * (float)(count - 1))) * 0.5f;
        for (i = 0; i < key_count; i++)
        {
            if (keys[i].row != row)
                continue;
            out[i] = (ui_rect){x, top + (float)row * 66.0f, unit * keys[i].width,
                               height};
            x += unit * keys[i].width + gap;
        }
    }
}

static void build_keys(void)
{
    static const char *const rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm,."};
    int row, column;

    key_count = 0;
    for (row = 0; row < 3; row++)
    {
        for (column = 0; rows[row][column] != 0; column++)
        {
            ui_key *key = &keys[key_count++];

            key->label = NULL;
            key->kind = KEY_CHAR;
            key->text = rows[row][column];
            key->row = row;
            key->width = 1.0f;
        }
    }
    keys[key_count++] = (ui_key){"space", KEY_SPACE, ' ', 3, 5.0f};
    keys[key_count++] = (ui_key){"delete", KEY_BACKSPACE, 0, 3, 1.6f};
    keys[key_count++] = (ui_key){"cancel", KEY_CANCEL, 0, 3, 1.6f};
    keys[key_count++] = (ui_key){"go", KEY_GO, 0, 3, 1.6f};
}

static void key_label(const ui_key *key, char *buffer, size_t size)
{
    if (key->kind == KEY_CHAR)
    {
        buffer[0] = key->text;
        buffer[1] = '\0';
    }
    else
    {
        snprintf(buffer, size, "%s", key->label != NULL ? key->label : "");
    }
}

/* --------------------------------------------------------------- drawing */

/* Alpha-blends a 64x64 icon into box with nearest-neighbour scaling. */
static void blit_icon(ui_canvas *canvas, const uint32_t *icon, ui_rect box)
{
    int x, y;

    if (icon == NULL || box.w < 1.0f || box.h < 1.0f)
        return;
    for (y = 0; y < (int)box.h; y++)
    {
        int sy = y * FAVICON_SIZE / (int)box.h;
        uint32_t *row = canvas->pixels + (size_t)((int)box.y + y) * canvas->stride +
                        (int)box.x;
        for (x = 0; x < (int)box.w; x++)
        {
            int sx = x * FAVICON_SIZE / (int)box.w;
            uint32_t src = icon[sy * FAVICON_SIZE + sx];
            uint32_t alpha = src >> 24;
            uint32_t dst;
            uint32_t r, g, b;

            if (alpha == 0)
                continue;
            if (alpha == 255)
            {
                row[x] = src;
                continue;
            }
            dst = row[x];
            r = ((src & 0xff) * alpha + (dst & 0xff) * (255 - alpha)) / 255;
            g = (((src >> 8) & 0xff) * alpha + ((dst >> 8) & 0xff) * (255 - alpha)) / 255;
            b = (((src >> 16) & 0xff) * alpha + ((dst >> 16) & 0xff) * (255 - alpha)) / 255;
            row[x] = (0xffu << 24) | (b << 16) | (g << 8) | r;
        }
    }
}

/* Firefox-style tab strip: group chip, tabs, new-tab button, group underline. */
static void draw_tab_strip(ui_canvas *canvas)
{
    float height = chrome_height() - 84.0f;
    float tab_width = 210.0f;
    float x = 14.0f;
    float group_start;
    int count = ui.tab_count > 0 ? ui.tab_count : 1;
    int i;

    ui_fill_rect(canvas, (ui_rect){0.0f, 0.0f, (float)ui.width, height}, 0.0f,
                 theme()->page);

    if (ui.group_enabled)
    {
        float label_width = text_width(&ui.regular, 17.0f, ui.group_name) + 42.0f;
        ui_rect chip = {x, 8.0f, label_width, 26.0f};

        ui_fill_rect(canvas, chip, 13.0f, theme()->surface);
        ui_circle(canvas, chip.x + 14.0f, chip.y + 13.0f, 5.0f,
                  ui_rgb(ui.group_color));
        text_at(canvas, &ui.regular, 17.0f, chip.x + 26.0f, chip.y + 19.0f,
                theme()->text_muted, ui.group_name);
        x += label_width + 10.0f;
    }
    group_start = x;

    for (i = 0; i < count; i++)
    {
        ui_rect tab = {x, 4.0f, tab_width, height - 8.0f};
        bool active = (i == ui.tab_active) || ui.tab_count == 0;
        const char *title = ui.tab_count > 0 ? ui.tab_titles[i] : "New Tab";
        const uint32_t *icon = active ? favicons_get(ui.address) : NULL;

        ui_fill_rect(canvas, tab, 9.0f,
                     active ? theme()->surface : theme()->page);
        if (active)
            ui_rect_outline(canvas, tab, 9.0f, 1.0f, theme()->outline);
        if (icon != NULL)
            blit_icon(canvas, icon, (ui_rect){tab.x + 12.0f, tab.y + 7.0f,
                                              20.0f, 20.0f});
        else
        {
            ui_circle(canvas, tab.x + 22.0f, tab.y + 17.0f, 8.0f,
                      ui_with_alpha(theme()->accent, 0.18f));
            ui_circle(canvas, tab.x + 22.0f, tab.y + 17.0f, 3.0f,
                      theme()->accent);
        }
        ui_text_draw_elided(canvas, &ui.regular, 18.0f, tab.x + 40.0f,
                            tab.y + 23.0f, active ? theme()->text : theme()->text_muted,
                            title, tab.w - 66.0f);
        if (active)
            ui_icon(canvas, UI_ICON_CLOSE,
                    (ui_rect){tab.x + tab.w - 26.0f, tab.y + 9.0f, 14.0f, 14.0f},
                    theme()->text_muted);
        x += tab_width + 4.0f;
    }

    ui_rect plus = {x + 4.0f, 8.0f, 30.0f, 26.0f};
    ui_fill_rect(canvas, plus, 8.0f, theme()->surface);
    ui_icon(canvas, UI_ICON_PLUS, (ui_rect){plus.x + 8.0f, plus.y + 7.0f,
                                            14.0f, 14.0f}, theme()->text_muted);

    if (ui.group_enabled)
        ui_fill_rect(canvas, (ui_rect){group_start, height - 3.0f,
                                       x - group_start - 4.0f, 3.0f},
                     1.5f, ui_rgb(ui.group_color));
}

static void draw_chrome(ui_canvas *canvas)
{
    ui_rect bar = {0.0f, 0.0f, (float)ui.width, chrome_height()};
    ui_rect line = {0.0f, chrome_height() - 1.0f, (float)ui.width, 1.0f};
    ui_rect address;
    ui_icon_kind icon;
    ui_colour ink;
    const char *shown;
    bool placeholder;
    float baseline;
    int i;

    ui_fill_rect(canvas, bar, 0.0f, theme()->surface);
    ui_fill_rect(canvas, (ui_rect){0.0f, 0.0f, (float)ui.width,
                                   chrome_height() - 84.0f},
                 0.0f, theme()->page);
    draw_tab_strip(canvas);
    ui_fill_rect(canvas, line, 0.0f, theme()->outline);

    for (i = 0; i < CHROME_COUNT; i++)
    {
        ui_rect box;
        ui_icon_kind kind;
        bool enabled = true;
        bool focused = (ui.focus == FOCUS_CHROME && ui.chrome_focus == i);

        if (i == CHROME_ADDRESS)
            continue;
        box = chrome_button_box(i);
        switch (i)
        {
        case CHROME_BACK:
            kind = UI_ICON_BACK;
            enabled = ui.can_back;
            break;
        case CHROME_FORWARD:
            kind = UI_ICON_FORWARD;
            enabled = ui.can_forward;
            break;
        case CHROME_RELOAD:
            kind = ui.loading ? UI_ICON_STOP : UI_ICON_RELOAD;
            break;
        default:
            kind = UI_ICON_MENU;
            break;
        }
        if (focused)
            ui_fill_rect(canvas, box, 11.0f, ui_rgb(0x2e2e2e));
        ink = enabled ? ui_rgb(0xd8d8d8) : ui_rgba(0xd8d8d8, 0.35f);
        ui_icon(canvas, kind, (ui_rect){box.x + 9.0f, box.y + 9.0f,
                                        box.w - 18.0f, box.h - 18.0f},
                ink);
        if (focused)
            focus_ring(canvas, box, 11.0f);
    }

    address = address_box();
    ui_fill_rect(canvas, address, 26.0f, theme()->surface_high);
    ui_rect_outline(canvas, address, 26.0f, 1.0f, theme()->outline);

    if (ui.focus == FOCUS_KEYBOARD && ui.input_for_address)
        shown = ui.input[0] != '\0' ? ui.input : "";
    else
        shown = ui.address[0] != '\0' ? ui.address : "";
    /* A load in flight has no committed URL yet (the window still reports
     * the page it is leaving), so the bar shows the target it was given. */
    if (shown[0] == '\0' && ui.loading && ui.nav_target[0] != '\0')
        shown = ui.nav_target;
    if (shown[0] == '\0' && ui.focus != FOCUS_KEYBOARD)
    {
        shown = "Search or enter address";
        placeholder = true;
    }
    else
    {
        placeholder = false;
    }

    if (strncmp(shown, "https:", 6) == 0)
        icon = UI_ICON_LOCK;
    else if (placeholder)
        icon = UI_ICON_SEARCH;
    else
        icon = UI_ICON_GLOBE;
    ui_icon(canvas, icon, (ui_rect){address.x + 18.0f, address.y + 15.0f,
                                    22.0f, 22.0f},
            ui_rgb(0xaaaaaa));

    ink = placeholder ? theme()->text_muted : theme()->text;
    baseline = address.y + address.h * 0.5f + 9.0f;
    ui_text_draw_elided(canvas, &ui.regular, 25.0f, address.x + 50.0f, baseline,
                        ink, shown, address.w - 76.0f);

    if (ui.focus == FOCUS_KEYBOARD && ui.input_for_address && fmodf(ui.caret, 1.06f) < 0.53f)
    {
        float pen = address.x + 50.0f +
                    ui_text_width(&ui.regular, 25.0f, shown);
        ui_rect caret = {pen + 1.0f, address.y + 15.0f, 2.0f, address.h - 30.0f};

        ui_fill_rect(canvas, caret, 1.0f, theme()->text);
    }
    if (ui.focus == FOCUS_CHROME && ui.chrome_focus == CHROME_ADDRESS)
        focus_ring(canvas, address, 26.0f);

    if (ui.loading || ui.progress > 0.0f)
    {
        float width = (float)ui.width * ui.progress;
        ui_rect progress = {0.0f, chrome_height() - 3.0f, width, 3.0f};

        ui_fill_rect(canvas, progress, 1.5f, theme()->accent);
    }
}

/* Home screen geometry, shared by the painter and the cursor hit tests so a
 * click always lands on what the player sees. */
static ui_rect home_search_rect(void)
{
    ui_rect area = content_area();

    return (ui_rect){(float)ui.width * 0.5f - 500.0f, area.y + 90.0f,
                     1000.0f, 78.0f};
}

static ui_rect home_tile_rect(int index)
{
    const float tile = 150.0f;
    const float gap = 28.0f;
    float grid_width = (float)QUICK_LINK_COUNT * tile +
                       (float)(QUICK_LINK_COUNT - 1) * gap;
    float grid_x = (float)ui.width * 0.5f - grid_width * 0.5f;

    return (ui_rect){grid_x + (float)index * (tile + gap),
                     content_area().y + 250.0f, tile, tile};
}

static void draw_home(ui_canvas *canvas)
{
    ui_rect area = content_area();
    float cx = (float)ui.width * 0.5f;
    float top = area.y;
    ui_rect search;
    float tile = 150.0f;
    int i;

    ui_fill_rect(canvas, area, 0.0f, theme()->page);

    /* PlayKit brand above the search bar. The baseline clears the chrome bar
     * so the whole word stays visible. */
    text_centred(canvas, &ui.medium, 44.0f, cx, top + 58.0f, theme()->text,
                 "PlayKit");

    /* Search sits at the top of the page, under the chrome bar. */
    search = home_search_rect();
    ui_shadow(canvas, search, 22.0f, 22.0f, 10.0f, theme()->shadow);
    ui_fill_rect(canvas, search, 22.0f, theme()->surface);
    ui_rect_outline(canvas, search, 22.0f, 1.0f, theme()->outline);
    ui_icon(canvas, UI_ICON_SEARCH, (ui_rect){search.x + 24.0f, search.y + 26.0f,
                                              26.0f, 26.0f},
            theme()->text_muted);
    text_at(canvas, &ui.regular, 27.0f, search.x + 68.0f,
            search.y + search.h * 0.5f + 10.0f, theme()->text_muted,
            "Search with PlayKit or enter address");
    if (ui.screen == SCREEN_HOME && ui.home_focus == 0)
        focus_ring(canvas, search, 22.0f);

    /* Site tiles, one row, label under each square. */
    for (i = 0; i < QUICK_LINK_COUNT; i++)
    {
        ui_rect box = home_tile_rect(i);
        bool focused = (ui.screen == SCREEN_HOME && ui.home_focus == i + 1);
        ui_colour tint = ui_rgb(quick_links[i].tint);

        const uint32_t *icon = favicons_get(quick_links[i].url);

        ui_fill_rect(canvas, box, 20.0f,
                     focused ? theme()->surface_high : theme()->surface);
        ui_rect_outline(canvas, box, 20.0f, 1.0f, theme()->outline);
        if (icon != NULL)
            blit_icon(canvas, icon, (ui_rect){box.x + tile * 0.5f - 30.0f,
                                              box.y + tile * 0.5f - 30.0f,
                                              60.0f, 60.0f});
        else
        {
            ui_circle(canvas, box.x + tile * 0.5f, box.y + tile * 0.5f, 34.0f,
                      ui_with_alpha(tint, 0.16f));
            ui_icon(canvas, UI_ICON_GLOBE,
                    (ui_rect){box.x + tile * 0.5f - 18.0f,
                              box.y + tile * 0.5f - 18.0f, 36.0f, 36.0f},
                    tint);
        }
        text_centred(canvas, &ui.regular, 22.0f, box.x + tile * 0.5f,
                     box.y + tile + 34.0f, theme()->text, quick_links[i].label);
        if (focused)
            focus_ring(canvas, box, 20.0f);
    }

    /* Bookmarks row, the same place the reference puts its recommendations. */
    if (ui.bookmark_count > 0)
    {
        float card_width = 560.0f;
        float card_height = 210.0f;
        float card_gap = 30.0f;
        float row_width;
        int shown = ui.bookmark_count < 3 ? ui.bookmark_count : 3;

        row_width = (float)shown * card_width + (float)(shown - 1) * card_gap;
        text_at(canvas, &ui.medium, 28.0f, cx - row_width * 0.5f,
                top + 510.0f, theme()->text, "Bookmarks");

        for (i = 0; i < shown; i++)
        {
            ui_rect card = {cx - row_width * 0.5f +
                                (float)i * (card_width + card_gap),
                            top + 540.0f, card_width, card_height};
            const bookmark_entry *entry = &ui.bookmarks[i];

            ui_fill_rect(canvas, card, 18.0f, theme()->surface);
            ui_rect_outline(canvas, card, 18.0f, 1.0f, theme()->outline);
            ui_fill_rect(canvas, (ui_rect){card.x + 24.0f, card.y + 24.0f,
                                           card.w - 48.0f, 92.0f},
                         14.0f, ui_with_alpha(theme()->accent, 0.12f));
            ui_icon(canvas, UI_ICON_GLOBE,
                    (ui_rect){card.x + 40.0f, card.y + 52.0f, 36.0f, 36.0f},
                    theme()->accent);
            text_at(canvas, &ui.medium, 25.0f, card.x + 24.0f,
                    card.y + 152.0f, theme()->text,
                    entry->title[0] != '\0' ? entry->title : entry->url);
            ui_text_draw_elided(canvas, &ui.regular, 18.0f, card.x + 24.0f,
                                card.y + 182.0f, theme()->text_muted,
                                entry->url, card.w - 48.0f);
        }
    }

    /* Controller hints, centred near the bottom. */
    {
        struct
        {
            ui_glyph_kind glyph;
            const char *label;
        } hints[] = {
            {UI_GLYPH_CROSS, "Open"},
            {UI_GLYPH_TRIANGLE, "Address"},
            {UI_GLYPH_SQUARE, "Menu"},
            {UI_GLYPH_CIRCLE, "Back"},
        };
        float widths[4];
        float total = 0.0f;
        float x;
        int count = 4;

        for (i = 0; i < count; i++)
        {
            widths[i] = 34.0f + 10.0f + text_width(&ui.regular, 20.0f, hints[i].label);
            total += widths[i] + (i + 1 < count ? 34.0f : 0.0f);
        }
        x = cx - total * 0.5f;
        for (i = 0; i < count; i++)
        {
            ui_button_glyph(canvas, (ui_rect){x, (float)ui.height - 58.0f, 30.0f, 30.0f},
                            hints[i].glyph, theme()->surface_high, theme()->text);
            text_at(canvas, &ui.regular, 20.0f, x + 40.0f,
                    (float)ui.height - 37.0f, theme()->text_muted, hints[i].label);
            x += widths[i] + 34.0f;
        }
    }
}

static void draw_keyboard(ui_canvas *canvas)
{
    ui_rect panel = keyboard_panel();
    ui_rect field = {panel.x + 36.0f, panel.y + 62.0f, panel.w - 72.0f, 64.0f};
    ui_rect rects[UI_MAX_KEYS];
    const char *title = ui.input_for_address ? "Address" : "Search";
    float baseline = field.y + 42.0f;
    int i;

    ui_fill_rect(canvas, (ui_rect){0, 0, (float)ui.width, (float)ui.height},
                 0.0f, ui_rgba(0x000000, 0.55f));
    ui_shadow(canvas, panel, 26.0f, 40.0f, 16.0f, theme()->shadow);
    ui_fill_rect(canvas, panel, 26.0f, ui_rgb(0x1d1d1d));
    ui_rect_outline(canvas, panel, 26.0f, 1.0f, ui_rgb(0x343434));

    text_at(canvas, &ui.regular, 20.0f, panel.x + 38.0f, panel.y + 42.0f,
            theme()->text_muted, title);

    ui_fill_rect(canvas, field, 16.0f, ui_rgb(0x111111));
    ui_rect_outline(canvas, field, 16.0f, 1.0f, ui_rgb(0x3a3a3a));
    if (ui.input[0] == '\0')
        text_at(canvas, &ui.regular, 28.0f, field.x + 20.0f, baseline,
                ui_with_alpha(theme()->text_muted, 0.7f), "Type, then Go");
    ui_text_draw_elided(canvas, &ui.mono, 28.0f, field.x + 20.0f, baseline,
                        theme()->text, ui.input, field.w - 40.0f);
    if (fmodf(ui.caret, 1.06f) < 0.53f)
    {
        float pen = field.x + 20.0f + ui_text_width(&ui.mono, 28.0f, ui.input);
        ui_rect caret = {pen + 2.0f, field.y + 16.0f, 2.0f, field.h - 32.0f};

        ui_fill_rect(canvas, caret, 1.0f, theme()->text);
    }
    focus_ring(canvas, field, 16.0f);

    layout_keys(panel, rects);
    for (i = 0; i < key_count; i++)
    {
        ui_rect key = rects[i];
        bool focused = (ui.key_focus == i);
        bool accent = keys[i].kind == KEY_GO;
        ui_colour fill = accent ? theme()->accent
                                : (focused ? ui_rgb(0x3d3d3d) : ui_rgb(0x2e2e2e));
        ui_colour ink = accent ? theme()->on_primary : theme()->text;
        char label[16];

        ui_fill_rect(canvas, key, 12.0f, fill);
        if (!accent)
            ui_rect_outline(canvas, key, 12.0f, 1.0f, ui_rgb(0x444444));
        key_label(&keys[i], label, sizeof(label));
        text_centred(canvas, keys[i].kind == KEY_CHAR ? &ui.medium : &ui.regular,
                     24.0f, key.x + key.w * 0.5f, key.y + key.h * 0.5f + 9.0f,
                     ink, label);
        if (focused)
            focus_ring(canvas, key, 12.0f);
    }

    text_at(canvas, &ui.regular, 18.0f, panel.x + panel.w - 38.0f -
                                             text_width(&ui.regular, 18.0f,
                                                        "X type   Square delete   O cancel"),
            panel.y + panel.h - 12.0f, theme()->text_muted,
            "X type   Square delete   O cancel");
}

static void draw_menu(ui_canvas *canvas)
{
    static const struct
    {
        ui_icon_kind icon;
        const char *label;
    } rows[MENU_COUNT] = {
        {UI_ICON_TABS, "Tabs"},
        {UI_ICON_STAR, "Bookmarks"},
        {UI_ICON_DOWNLOAD, "Downloads"},
        {UI_ICON_PLUS, "Add bookmark"},
        {UI_ICON_KEYBOARD, "Address bar"},
        {UI_ICON_KEYBOARD, "Page keyboard"},
        {UI_ICON_HOME, "Home"},
        {UI_ICON_RELOAD, "Reload"},
        {UI_ICON_PLUS, "Zoom in"},
        {UI_ICON_MINUS, "Zoom out"},
        {UI_ICON_STAR, "Dark / light mode"},
        {UI_ICON_TABS, "Tab group"},
        {UI_ICON_QUIT, "Quit"},
    };
    ui_rect panel = menu_panel();
    int i;

    ui_shadow(canvas, panel, 20.0f, 30.0f, 14.0f, theme()->shadow);
    ui_fill_rect(canvas, panel, 20.0f, theme()->surface);
    ui_rect_outline(canvas, panel, 20.0f, 1.0f, theme()->outline);
    text_at(canvas, &ui.regular, 18.0f, panel.x + 22.0f, panel.y + 34.0f,
            theme()->text_muted, "Menu");

    for (i = 0; i < MENU_COUNT; i++)
    {
        ui_rect row = {panel.x + 8.0f, panel.y + 48.0f + (float)i * 64.0f,
                       panel.w - 16.0f, 56.0f};
        bool focused = (ui.menu_focus == i);

        if (focused)
            ui_fill_rect(canvas, row, 12.0f, theme()->surface_high);
        ui_icon(canvas, rows[i].icon, (ui_rect){row.x + 16.0f, row.y + 16.0f,
                                                24.0f, 24.0f},
                focused ? theme()->text : theme()->text_muted);
        text_at(canvas, &ui.regular, 23.0f, row.x + 54.0f, row.y + 36.0f,
                theme()->text, rows[i].label);
    }
}

static void draw_status(ui_canvas *canvas)
{
    float width;

    if (ui.status[0] == '\0' || ui.time > ui.status_until)
        return;
    width = text_width(&ui.regular, 20.0f, ui.status) + 56.0f;
    if (width > (float)ui.width - 48.0f)
        width = (float)ui.width - 48.0f;
    {
        ui_rect pill = {24.0f, (float)ui.height - 76.0f, width, 44.0f};

        ui_shadow(canvas, pill, 22.0f, 18.0f, 8.0f, theme()->shadow);
        ui_fill_rect(canvas, pill, 22.0f, ui_with_alpha(theme()->surface, 0.96f));
        ui_rect_outline(canvas, pill, 22.0f, 1.0f, theme()->outline);
        ui_text_draw_elided(canvas, &ui.regular, 20.0f, pill.x + 28.0f,
                            pill.y + 29.0f, theme()->text, ui.status,
                            pill.w - 56.0f);
    }
}

/* ----------------------------------------------------------------- input */

static bool pressed(const ps5_pad_state *pad, uint32_t button)
{
    return (pad->pressed & button) != 0;
}

static void handle_chrome_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_LEFT))
        ui.chrome_focus = (ui.chrome_focus + CHROME_COUNT - 1) % CHROME_COUNT;
    if (pressed(pad, PS5_PAD_RIGHT))
        ui.chrome_focus = (ui.chrome_focus + 1) % CHROME_COUNT;
    if (pressed(pad, PS5_PAD_UP))
    {
        ui.focus = FOCUS_TABS;
        ui.tab_focus = ui.tab_active;
        return;
    }
    if (pressed(pad, PS5_PAD_DOWN) || pressed(pad, PS5_PAD_CIRCLE))
    {
        ui.focus = FOCUS_PAGE;
        return;
    }
    if (pressed(pad, PS5_PAD_OPTIONS))
    {
        open_menu();
        return;
    }
    if (pressed(pad, PS5_PAD_CROSS))
    {
        switch (ui.chrome_focus)
        {
        case CHROME_BACK:
            if (ui.can_back)
                set_action(BROWSER_UI_BACK);
            break;
        case CHROME_FORWARD:
            if (ui.can_forward)
                set_action(BROWSER_UI_FORWARD);
            break;
        case CHROME_RELOAD:
            set_action(ui.loading ? BROWSER_UI_STOP : BROWSER_UI_RELOAD);
            break;
        case CHROME_ADDRESS:
            open_keyboard(true, ui.address);
            break;
        case CHROME_MENU:
            open_menu();
            break;
        default:
            break;
        }
    }
}

static void handle_home_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_OPTIONS))
    {
        open_menu();
        return;
    }
    if (pressed(pad, PS5_PAD_TRIANGLE))
    {
        ui.focus = FOCUS_CHROME;
        ui.chrome_focus = CHROME_ADDRESS;
        return;
    }

    /* Virtual cursor click. The touchpad always clicks at the cursor; Cross
     * does too once the stick has moved the cursor, so D-pad navigation
     * still works before that. */
    if (pressed(pad, PS5_PAD_TOUCH_PAD) ||
        (ui.cursor_active && pressed(pad, PS5_PAD_CROSS)))
    {
        ui_rect search = home_search_rect();
        int i;

        if (ui.cursor_x >= search.x && ui.cursor_x < search.x + search.w &&
            ui.cursor_y >= search.y && ui.cursor_y < search.y + search.h)
        {
            open_keyboard(true, "");
            return;
        }
        for (i = 0; i < QUICK_LINK_COUNT; i++)
        {
            ui_rect box = home_tile_rect(i);

            if (ui.cursor_x >= box.x && ui.cursor_x < box.x + box.w &&
                ui.cursor_y >= box.y && ui.cursor_y < box.y + box.h)
            {
                ui.home_focus = i + 1;
                navigate_to(quick_links[i].url);
                return;
            }
        }
        return; /* a click on empty space does nothing */
    }

    if (pressed(pad, PS5_PAD_UP) && ui.home_focus > 0)
        ui.home_focus = 0;
    if (pressed(pad, PS5_PAD_DOWN) && ui.home_focus == 0)
        ui.home_focus = 1;
    if (pressed(pad, PS5_PAD_LEFT) && ui.home_focus > 1)
        ui.home_focus--;
    if (pressed(pad, PS5_PAD_RIGHT) && ui.home_focus >= 1 &&
        ui.home_focus < QUICK_LINK_COUNT)
        ui.home_focus++;
    if (pressed(pad, PS5_PAD_CROSS))
    {
        if (ui.home_focus == 0)
            open_keyboard(true, "");
        else
            navigate_to(quick_links[ui.home_focus - 1].url);
    }
    if (pressed(pad, PS5_PAD_L1) && ui.can_back)
        set_action(BROWSER_UI_BACK);
    if (pressed(pad, PS5_PAD_R1) && ui.can_forward)
        set_action(BROWSER_UI_FORWARD);
}

static void move_key_focus(int dx, int dy, const ui_rect *rects)
{
    ui_rect current = rects[ui.key_focus];
    float cx = current.x + current.w * 0.5f;
    float cy = current.y + current.h * 0.5f;
    int best = ui.key_focus;
    float best_score = 0.0f;
    int i;

    for (i = 0; i < key_count; i++)
    {
        ui_rect other = rects[i];
        float ox = other.x + other.w * 0.5f;
        float oy = other.y + other.h * 0.5f;
        float along, across, score;

        if (i == ui.key_focus)
            continue;
        if (dx != 0)
        {
            along = (ox - cx) * (float)dx;
            across = fabsf(oy - cy);
            if (along <= 1.0f || across > current.h * 1.4f)
                continue;
            score = along + across * 2.4f;
        }
        else
        {
            along = (oy - cy) * (float)dy;
            across = fabsf(ox - cx);
            if (along <= 1.0f)
                continue;
            score = along + across * 1.6f;
        }
        if (best_score == 0.0f || score < best_score)
        {
            best_score = score;
            best = i;
        }
    }
    ui.key_focus = best;
}

static void handle_keyboard_pad(const ps5_pad_state *pad)
{
    ui_rect rects[UI_MAX_KEYS];

    layout_keys(keyboard_panel(), rects);

    if (pressed(pad, PS5_PAD_LEFT))
        move_key_focus(-1, 0, rects);
    if (pressed(pad, PS5_PAD_RIGHT))
        move_key_focus(1, 0, rects);
    if (pressed(pad, PS5_PAD_UP))
        move_key_focus(0, -1, rects);
    if (pressed(pad, PS5_PAD_DOWN))
        move_key_focus(0, 1, rects);
    if (pressed(pad, PS5_PAD_SQUARE) || pressed(pad, PS5_PAD_L1))
        backspace_input();
    if (pressed(pad, PS5_PAD_CIRCLE))
    {
        close_keyboard();
        return;
    }
    if (!pressed(pad, PS5_PAD_CROSS))
        return;

    switch (keys[ui.key_focus].kind)
    {
    case KEY_CHAR:
    {
        char text[2] = {keys[ui.key_focus].text, 0};

        append_input(text);
        break;
    }
    case KEY_SPACE:
        append_input(" ");
        break;
    case KEY_BACKSPACE:
        backspace_input();
        break;
    case KEY_CANCEL:
        close_keyboard();
        break;
    case KEY_GO:
    {
        char url[UI_MAX_URL];

        resolve_address(ui.input, url, sizeof(url));
        if (url[0] != '\0')
            navigate_to(url);
        else
            close_keyboard();
        break;
    }
    }
}

static void handle_menu_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_UP))
        ui.menu_focus = (ui.menu_focus + MENU_COUNT - 1) % MENU_COUNT;
    if (pressed(pad, PS5_PAD_DOWN))
        ui.menu_focus = (ui.menu_focus + 1) % MENU_COUNT;
    if (pressed(pad, PS5_PAD_CIRCLE) || pressed(pad, PS5_PAD_LEFT) ||
        pressed(pad, PS5_PAD_OPTIONS))
    {
        ui.focus = FOCUS_PAGE;
        return;
    }
    if (!pressed(pad, PS5_PAD_CROSS))
        return;

    switch (ui.menu_focus)
    {
    case MENU_TABS:
        ui.focus = FOCUS_TABS;
        ui.tab_focus = ui.tab_active;
        ui.list_scroll = 0;
        break;
    case MENU_BOOKMARKS:
        ui.focus = FOCUS_BOOKMARKS;
        ui.bookmark_focus = 0;
        ui.list_scroll = 0;
        break;
    case MENU_DOWNLOADS:
        scan_downloads();
        ui.focus = FOCUS_DOWNLOADS;
        ui.download_focus = 0;
        ui.list_scroll = 0;
        break;
    case MENU_ADD_BOOKMARK:
        add_bookmark();
        break;
    case MENU_ADDRESS:
        open_keyboard(true, ui.address);
        break;
    case MENU_KEYBOARD:
        set_action(BROWSER_UI_OSK);
        break;
    case MENU_HOME:
        go_home_screen();
        set_action(BROWSER_UI_HOME);
        break;
    case MENU_RELOAD:
        set_action(BROWSER_UI_RELOAD);
        break;
    case MENU_ZOOM_IN:
        set_action(BROWSER_UI_ZOOM_IN);
        break;
    case MENU_ZOOM_OUT:
        set_action(BROWSER_UI_ZOOM_OUT);
        break;
    case MENU_THEME:
        ui.light_mode = !ui.light_mode;
        browser_ui_show_status(ui.light_mode ? "Light mode" : "Dark mode");
        break;
    case MENU_GROUP:
        ui.group_enabled = !ui.group_enabled;
        browser_ui_show_status(ui.group_enabled ? "Tab group shown" : "Tab group hidden");
        break;
    case MENU_QUIT:
        set_action(BROWSER_UI_QUIT);
        break;
    default:
        break;
    }
}

/* Panel rows keep their focus and the panel closes on circle. */
static void handle_tabs_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_CIRCLE) || pressed(pad, PS5_PAD_OPTIONS) ||
        pressed(pad, PS5_PAD_LEFT))
    {
        ui.focus = FOCUS_PAGE;
        return;
    }
    if (pressed(pad, PS5_PAD_DOWN))
    {
        ui.focus = FOCUS_CHROME;
        return;
    }
    move_list_focus(&ui.tab_focus, ui.tab_count, pad);
    if (pressed(pad, PS5_PAD_TRIANGLE))
    {
        set_action(BROWSER_UI_TAB_NEW);
        ui.focus = FOCUS_PAGE;
        return;
    }
    if (ui.tab_count == 0)
        return;
    if (pressed(pad, PS5_PAD_SQUARE))
    {
        ui.action = BROWSER_UI_TAB_CLOSE;
        ui.action_index = ui.tab_focus;
        ui.focus = FOCUS_PAGE;
        return;
    }
    if (pressed(pad, PS5_PAD_CROSS))
    {
        if (ui.tab_focus != ui.tab_active)
        {
            ui.action = BROWSER_UI_TAB_SWITCH;
            ui.action_index = ui.tab_focus;
        }
        ui.focus = FOCUS_PAGE;
    }
}

static void handle_bookmarks_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_CIRCLE) || pressed(pad, PS5_PAD_OPTIONS) ||
        pressed(pad, PS5_PAD_LEFT))
    {
        ui.focus = FOCUS_PAGE;
        return;
    }
    move_list_focus(&ui.bookmark_focus, ui.bookmark_count, pad);
    if (!pressed(pad, PS5_PAD_CROSS) || ui.bookmark_count == 0)
        return;
    if (pressed(pad, PS5_PAD_SQUARE))
    {
        remove_bookmark(ui.bookmark_focus);
        return;
    }
    navigate_to(ui.bookmarks[ui.bookmark_focus].url);
}

static void handle_downloads_pad(const ps5_pad_state *pad)
{
    if (pressed(pad, PS5_PAD_CIRCLE) || pressed(pad, PS5_PAD_OPTIONS) ||
        pressed(pad, PS5_PAD_LEFT))
    {
        ui.focus = FOCUS_PAGE;
        return;
    }
    move_list_focus(&ui.download_focus, ui.download_count, pad);
    if (pressed(pad, PS5_PAD_CROSS))
        browser_ui_show_status("Downloads are saved in /download0/downloads");
}

/* ------------------------------------------------------------ public API */

bool browser_ui_init(int width, int height, const char *font_dir)
{
    char path[512];

    memset(&ui, 0, sizeof(ui));
    ui.width = width;
    ui.height = height;
    ui.screen = SCREEN_HOME;
    ui.focus = FOCUS_PAGE;
    ui.home_focus = 0;
    ui.chrome_focus = CHROME_ADDRESS;

    snprintf(path, sizeof(path), "%s/google-sans-regular.huifont", font_dir);
    if (!ui_font_load(&ui.regular, path))
    {
        fprintf(stderr, "browser_ui: unable to load %s\n", path);
        return false;
    }
    snprintf(path, sizeof(path), "%s/google-sans-medium.huifont", font_dir);
    if (!ui_font_load(&ui.medium, path))
    {
        fprintf(stderr, "browser_ui: unable to load %s\n", path);
        return false;
    }
    snprintf(path, sizeof(path), "%s/dejavu-sans-mono.huifont", font_dir);
    if (!ui_font_load(&ui.mono, path))
    {
        /* the address field falls back to the regular face */
        snprintf(path, sizeof(path), "%s/google-sans-regular.huifont", font_dir);
        if (!ui_font_load(&ui.mono, path))
            return false;
    }
    build_keys();
    resolve_state_path(ui.bookmark_path, sizeof(ui.bookmark_path),"PLAYKIT_BOOKMARKS", UI_STATE_DIR "/bookmarks.txt");
    resolve_state_path(ui.download_dir, sizeof(ui.download_dir),
                       "PLAYKIT_DOWNLOADS", UI_DOWNLOAD_DIR);
    load_bookmarks();
    ui.group_enabled = true;
    copy_string(ui.group_name, sizeof(ui.group_name), "Browsing");
    ui.group_color = 0x4a90e2;
    ui.cursor_x = ui.width * 0.5f;
    ui.cursor_y = ui.height * 0.5f;
    for (int i = 0; i < QUICK_LINK_COUNT; i++)
        favicons_prefetch(quick_links[i].url);
    ui.ready = true;
    return true;
}

void browser_ui_shutdown(void)
{
    if (!ui.ready)
        return;
    ui_font_free(&ui.regular);
    ui_font_free(&ui.medium);
    ui_font_free(&ui.mono);
    ui.ready = false;
}

void browser_ui_set_page(const char *url, const char *title)
{
    copy_string(ui.url, sizeof(ui.url), url);
    copy_string(ui.title, sizeof(ui.title), title);
    if (ui.screen == SCREEN_HOME)
        return;
    /* While a load is in flight the bar shows where it is going; once it
     * commits browser_ui_page_committed() puts the real URL there. */
    if (!ui.loading)
        copy_string(ui.address, sizeof(ui.address), url);
}

void browser_ui_page_committed(const char *url)
{
    if (url == NULL || url[0] == '\0')
        return;
    copy_string(ui.url, sizeof(ui.url), url);
    if (ui.screen != SCREEN_HOME)
        copy_string(ui.address, sizeof(ui.address), url);
    ui.loading = false;
    ui.progress = 1.0f;
    ui.load_finished = ui.time;
    ui.nav_target[0] = '\0';
}

void browser_ui_set_nav(bool can_back, bool can_forward)
{
    ui.can_back = can_back;
    ui.can_forward = can_forward;
}

bool browser_ui_is_home(void)
{
    return ui.screen == SCREEN_HOME;
}

void browser_ui_go_home(void)
{
    go_home_screen();
}

void browser_ui_show_status(const char *text)
{
    copy_string(ui.status, sizeof(ui.status), text);
    ui.status_until = ui.time + 4.0f;
}

bool browser_ui_handle_pad(const ps5_pad_state *pad, const ps5_pad_state *prev)
{
    (void)prev;
    if (!ui.ready || pad == NULL || !pad->connected)
        return false;

    /* ------------------------------------------------ virtual cursor */
    /* ps5_pad_poll already normalises the sticks to -1..1, so the axis
     * value is the direction. A small deadzone stops drift at rest. */
    {
        const float deadzone = 0.15f;
        float dx = pad->left_x;
        float dy = pad->left_y;

        if (fabsf(dx) < deadzone)
            dx = 0.0f;
        if (fabsf(dy) < deadzone)
            dy = 0.0f;

        if (dx != 0.0f || dy != 0.0f)
            ui.cursor_active = true;

        /* Square the response so small pushes are precise and full pushes
         * cross the screen quickly. */
        dx = dx * fabsf(dx);
        dy = dy * fabsf(dy);

        ui.cursor_x += dx * 1600.0f * (1.0f / 60.0f);
        ui.cursor_y += dy * 1100.0f * (1.0f / 60.0f);

        if (ui.cursor_x < 0.0f) ui.cursor_x = 0.0f;
        if (ui.cursor_x > (float)ui.width - 1.0f) ui.cursor_x = (float)ui.width - 1.0f;
        if (ui.cursor_y < 0.0f) ui.cursor_y = 0.0f;
        if (ui.cursor_y > (float)ui.height - 1.0f) ui.cursor_y = (float)ui.height - 1.0f;
    }

    bool clicked = pressed(pad, PS5_PAD_TOUCH_PAD) || pressed(pad, PS5_PAD_CROSS);

    /* Tab strip click: activate a tab, close it, or open a new one.
     * Only when the shell has focus (not while a menu or keyboard is open). */
    if (clicked && ui.focus == FOCUS_PAGE)
    {
        float strip_h = chrome_height() - 84.0f;
        if (ui.cursor_y < strip_h)
        {
            float x = 14.0f;
            if (ui.group_enabled)
            {
                float lw = text_width(&ui.regular, 17.0f, ui.group_name) + 42.0f;
                x += lw + 10.0f;
            }
            int count = ui.tab_count > 0 ? ui.tab_count : 1;
            int i;
            for (i = 0; i < count; i++)
            {
                float tw = 210.0f;
                if (ui.cursor_x >= x && ui.cursor_x < x + tw &&
                    ui.cursor_y >= 4.0f && ui.cursor_y < strip_h - 8.0f)
                {
                    if (ui.cursor_x > x + tw - 30.0f && ui.tab_count > 0)
                    {
                        /* close button area of the active tab */
                        if (i == ui.tab_active || ui.tab_count == 1)
                        {
                            /* remove tab */
                            int j;
                            for (j = i; j < ui.tab_count - 1; j++)
                            {
                                copy_string(ui.tab_titles[j], sizeof(ui.tab_titles[j]),
                                            ui.tab_titles[j + 1]);
                            }
                            ui.tab_count--;
                            if (ui.tab_active >= ui.tab_count)
                                ui.tab_active = ui.tab_count > 0 ? ui.tab_count - 1 : 0;
                            if (ui.tab_count == 0)
                                browser_ui_show_status("Tab closed");
                        }
                    }
                    else
                    {
                        ui.tab_active = i;
                        browser_ui_show_status(ui.tab_titles[i]);
                    }
                    return true;
                }
                x += tw + 4.0f;
            }
            /* new tab button */
            float nx = x + 4.0f;
            if (ui.cursor_x >= nx && ui.cursor_x < nx + 30.0f &&
                ui.cursor_y >= 8.0f && ui.cursor_y < 34.0f)
            {
                /* add a tab */
                if (ui.tab_count < UI_MAX_TABS)
                {
                    int idx = ui.tab_count;
                    copy_string(ui.tab_titles[idx], sizeof(ui.tab_titles[idx]),
                                "New Tab");
                    ui.tab_count++;
                    ui.tab_active = idx;
                    browser_ui_show_status("New tab");
                }
                return true;
            }
        }
    }

    switch (ui.focus)
    {
    case FOCUS_KEYBOARD:
        handle_keyboard_pad(pad);
        return true;
    case FOCUS_MENU:
        handle_menu_pad(pad);
        return true;
    case FOCUS_TABS:
        handle_tabs_pad(pad);
        return true;
    case FOCUS_BOOKMARKS:
        handle_bookmarks_pad(pad);
        return true;
    case FOCUS_DOWNLOADS:
        handle_downloads_pad(pad);
        return true;
    case FOCUS_CHROME:
        handle_chrome_pad(pad);
        return true;
    case FOCUS_PAGE:
        if (ui.screen == SCREEN_HOME)
        {
            handle_home_pad(pad);
            return true;
        }
        /* The page keeps the pad; the shell only takes the shortcuts that
         * the page has no use for. */
        if (pressed(pad, PS5_PAD_TRIANGLE) || pressed(pad, PS5_PAD_TOUCH_PAD))
        {
            ui.focus = FOCUS_CHROME;
            ui.chrome_focus = CHROME_ADDRESS;
            return true;
        }
        if (pressed(pad, PS5_PAD_OPTIONS))
        {
            open_menu();
            return true;
        }
        return false;
    }
    return false;
}

browser_ui_action browser_ui_take_action(char *url, size_t url_size, int *index)
{
    browser_ui_action action = ui.action;

    ui.action = BROWSER_UI_NONE;
    if (action == BROWSER_UI_NAVIGATE && url != NULL && url_size > 0)
        copy_string(url, url_size, ui.action_url);
    if (index != NULL)
        *index = ui.action_index;
    ui.action_index = 0;
    return action;
}

void browser_ui_set_tabs(int count, int active, const char *const *titles)
{
    int i;

    if (count > UI_MAX_TABS)
        count = UI_MAX_TABS;
    if (count < 0)
        count = 0;
    ui.tab_count = count;
    ui.tab_active = active >= 0 && active < count ? active : 0;
    for (i = 0; i < count; i++)
    {
        copy_string(ui.tab_titles[i], sizeof(ui.tab_titles[i]),
                    titles != NULL && titles[i] != NULL ? titles[i] : "");
    }
    if (ui.tab_focus >= count)
        ui.tab_focus = count > 0 ? count - 1 : 0;
}

const char *browser_ui_address(void)
{
    return ui.address;
}

bool browser_ui_pointer_visible(void)
{
    return ui.ready && ui.focus == FOCUS_PAGE && ui.screen == SCREEN_PAGE;
}

void browser_ui_render(uint32_t *frame, int width, int height, int stride,
                       float seconds)
{
    ui_canvas canvas = {frame, width, height, stride};

    if (!ui.ready)
        return;
    if (seconds < 0.0f)
        seconds = 0.0f;
    if (seconds > 0.25f)
        seconds = 0.25f;
    ui.time += seconds;
    ui.caret += seconds;
    ui.width = width;
    ui.height = height;

    if (ui.loading)
    {
        float elapsed = ui.time - ui.load_started;

        /* creep towards 90% while the page is on its way, then stop after a
         * while so a failed load does not leave the bar moving for ever */
        ui.progress = 0.02f + 0.88f * (1.0f - expf(-elapsed * 1.4f));
        if (elapsed > 12.0f)
        {
            ui.loading = false;
            ui.load_finished = ui.time;
        }
    }
    else if (ui.progress > 0.0f && ui.time - ui.load_finished > 0.35f)
    {
        ui.progress -= seconds * 2.5f;
        if (ui.progress < 0.0f)
            ui.progress = 0.0f;
    }

    if (ui.screen == SCREEN_HOME)
        draw_home(&canvas);
    draw_chrome(&canvas);
    if (ui.focus == FOCUS_KEYBOARD)
        draw_keyboard(&canvas);
    if (ui.focus == FOCUS_MENU)
        draw_menu(&canvas);
    else if (ui.focus == FOCUS_TABS)
        draw_tabs(&canvas);
    else if (ui.focus == FOCUS_BOOKMARKS)
        draw_bookmarks(&canvas);
    else if (ui.focus == FOCUS_DOWNLOADS)
        draw_downloads(&canvas);
    draw_status(&canvas);

    /* Crosshair / virtual cursor, always on top. Dark halo plus a white cross
     * stays visible on both themes and on top of favicons. */
    {
        float cx = ui.cursor_x, cy = ui.cursor_y;
        float size = 22.0f;
        ui_colour halo = ui_with_alpha(ui_rgb(0x000000), 0.6f);
        ui_colour c = ui_rgb(0xffffff);

        ui_fill_rect(&canvas, (ui_rect){cx - 2.0f, cy - size * 0.5f, 4.0f, size}, 0.0f, halo);
        ui_fill_rect(&canvas, (ui_rect){cx - size * 0.5f, cy - 2.0f, size, 4.0f}, 0.0f, halo);
        ui_fill_rect(&canvas, (ui_rect){cx - 1.0f, cy - size * 0.5f, 2.0f, size}, 0.0f, c);
        ui_fill_rect(&canvas, (ui_rect){cx - size * 0.5f, cy - 1.0f, size, 2.0f}, 0.0f, c);
        ui_fill_rect(&canvas, (ui_rect){cx - 3.5f, cy - 3.5f, 7.0f, 7.0f}, 3.5f, halo);
        ui_fill_rect(&canvas, (ui_rect){cx - 1.5f, cy - 1.5f, 3.0f, 3.0f}, 1.5f, c);
    }
}
