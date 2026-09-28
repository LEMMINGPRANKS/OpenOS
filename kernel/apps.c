#include "apps.h"
#include "shell.h"
#include "filemgr.h"
#include "ramfs.h"
#include "files.h"
#include "ext.h"
#include "term.h"
#include "wm.h"
#include "js.h"
#include "internet.h"
#include "timer.h"
#include "paint.h"
#include "settings.h"
#include "store.h"
#include "kupdate.h"
#include "music.h"
#include "dlapp.h"
#include "updapp.h"
#include "appbar.h"
#include "codeapp.h"

// Apps are the things that can live inside a window. Each app gets
// keyboard chars through app_input with the window's console.

#define NOTE_MAX 2048
#define NOTE_STATES 3

struct note_state {
    struct console *con;
    char buf[NOTE_MAX];
    int n;
    uint8_t used;
};

static struct note_state notes[NOTE_STATES];

static uint64_t last_note_flush;        // DR1 auto-save throttle

static char app_arg[32];              // filename for the viewer

void app_set_arg(const char *arg)
{
    for (int i = 0; i < 31 && arg[i]; i++)
        app_arg[i] = arg[i];
    app_arg[31] = 0;
}

const char *app_get_arg(void)
{
    return app_arg;
}

static void viewer_open(struct console *con)
{
    term_use(con);
    const struct ext_type *t = ext_lookup(app_arg);
    term_protect(con, APPBAR_ROWS);
    appbar_paint(con, app_arg, t->desc, 0x78909C);
    term_setcolor(t->vga_color);
    term_puts("type: ");
    term_puts(t->desc);
    term_puts("\n\n");
    uint32_t size = 0;
    const char *data = files_read(app_arg, &size);
    if (!data) {
        term_puts("(file not found)\n");
        return;
    }
    uint32_t cap = size > 4096 ? 4096 : size;
    for (uint32_t i = 0; i < cap; i++)
        term_putc(data[i]);
    if (size && data[size - 1] != '\n')
        term_putc('\n');
    if (size > cap) {
        term_puts("... (");
        char digits[12];
        int n = 0;
        uint32_t s = size - cap;
        while (s) { digits[n++] = (char)('0' + s % 10); s /= 10; }
        while (n) term_putc(digits[--n]);
        term_puts(" more bytes)\n");
    }
}

static void runner_open(struct console *con)
{
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    appbar_paint(con, app_arg, "OpenJS", 0x78909C);
    term_putc('\n');
    uint32_t size = 0;
    const char *data = files_read(app_arg, &size);
    if (!data) {
        term_puts("(file not found)\n");
        return;
    }
    char err[80];
    if (js_run(data, size, err, sizeof err) != 0)
        term_puts(err);
    else
        term_puts("\n(done)\n");
}

static int ends_with(const char *s, const char *suf)
{
    int n = 0, m = 0;
    while (s[n]) n++;
    while (suf[m]) m++;
    if (m > n)
        return 0;
    for (int i = 0; i < m; i++)
        if (s[n - m + i] != suf[i])
            return 0;
    return 1;
}

#define OPEN_WIN_W 448
#define OPEN_WIN_H 340

int open_file_window(const char *path)
{
    app_set_arg(path);
    if (ends_with(path, ".js"))
        return wm_open(APP_RUNNER, 110, 80, OPEN_WIN_W, OPEN_WIN_H);
    if (ends_with(path, ".cpp") || ends_with(path, ".hpp") ||
        ends_with(path, ".h"))
        return wm_open(APP_CODE, 110, 80, OPEN_WIN_W, OPEN_WIN_H);
    if (ends_with(path, ".html"))
        return wm_open(APP_INTERNET, 110, 80, OPEN_WIN_W, 424);
    if (ends_with(path, ".png"))
        return wm_open(APP_PAINT, 60, 40, PAINT_WIN_W, PAINT_WIN_H);
    if (ext_is_text(path))
        return wm_open(APP_VIEWER, 110, 80, OPEN_WIN_W, OPEN_WIN_H);
    return -1;                        // unknown/binary type
}

static struct note_state *note_for(struct console *con)
{
    for (int i = 0; i < NOTE_STATES; i++)
        if (notes[i].used && notes[i].con == con)
            return &notes[i];
    for (int i = 0; i < NOTE_STATES; i++)
        if (!notes[i].used) {
            notes[i].used = 1;
            notes[i].con = con;
            notes[i].n = 0;
            notes[i].buf[0] = 0;
            return &notes[i];
        }
    return &notes[0];
}

static void notepad_open(struct console *con)
{
    note_for(con);
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    appbar_paint(con, "note.txt", "auto-saved to DR1", 0xB8860B);
    term_puts("type your note -- it saves itself.\n\n");
    ramfs_write("/note.txt", "", 0);
}

static void notepad_input(struct console *con, char c)
{
    struct note_state *st = note_for(con);
    term_use(con);
    if (c == '\n' || c == '\b') {
        if (c == '\b' && st->n == 0)
            return;
        if (c == '\b')
            st->n--;
        else if (st->n < NOTE_MAX - 1)
            st->buf[st->n++] = '\n';
        term_putc(c);
    } else if (c >= 32 && c < 127 && st->n < NOTE_MAX - 1) {
        st->buf[st->n++] = c;
        term_putc(c);
    } else {
        return;                       // unchanged: no save needed
    }
    st->buf[st->n] = 0;
    ramfs_write("/note.txt", st->buf, (uint32_t)st->n);
    // DR1 auto-save: wait for a typing pause so we're not hitting the
    // disk on every keypress (100 Hz ticks, NOTE_FLUSH_TICKS = 2s)
    uint64_t now = timer_ticks();
    if (now - last_note_flush >= 200) {
        last_note_flush = now;
        store_flush();
    }
}

void app_open(enum app_id app, struct console *con)
{
    if (app == APP_SHELL)
        shell_app_open(con);
    else if (app == APP_NOTEPAD)
        notepad_open(con);
    else if (app == APP_FILES)
        filemgr_open(con);
    else if (app == APP_VIEWER)
        viewer_open(con);
    else if (app == APP_RUNNER)
        runner_open(con);
    else if (app == APP_INTERNET)
        internet_app_open(con);
    else if (app == APP_PAINT)
        paint_open(con);
    else if (app == APP_SETTINGS)
        settings_app_open(con);
    else if (app == APP_MUSIC)
        music_app_open(con);
    else if (app == APP_DOWNLOAD)
        download_app_open(con);
    else if (app == APP_UPDATE)
        update_app_open(con);
    else if (app == APP_CODE)
        code_open(con);
}

void app_input(enum app_id app, struct console *con, char c)
{
    if (app == APP_SHELL)
        shell_app_input(con, c);
    else if (app == APP_NOTEPAD)
        notepad_input(con, c);
    else if (app == APP_FILES)
        filemgr_input(con, c);
    else if (app == APP_INTERNET)
        internet_app_input(con, c);
    else if (app == APP_PAINT)
        paint_input(con, c);
    else if (app == APP_SETTINGS)
        settings_app_input(con, c);
    else if (app == APP_MUSIC)
        music_app_input(con, c);
    else if (app == APP_DOWNLOAD)
        download_app_input(con, c);
    else if (app == APP_UPDATE)
        update_app_input(con, c);
    else if (app == APP_CODE)
        code_input(con, c);
    (void)con; (void)c;                // other apps take no keyboard input
}

void app_click(enum app_id app, struct console *con, int mx, int my, int dbl)
{
    if (app == APP_FILES)
        filemgr_click(con, mx, my, dbl);
    else if (app == APP_INTERNET)
        internet_app_click(con, mx, my);
    else if (app == APP_DOWNLOAD)
        download_app_click(con, mx, my);
    else if (app == APP_MUSIC)
        music_app_click(con, mx, my);
    (void)mx; (void)my; (void)dbl;
}

int app_wants_pixels(enum app_id app)
{
    return app == APP_PAINT;
}

void app_repaint_pixels(enum app_id app, struct console *con,
                        uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (app == APP_PAINT)
        paint_repaint(con, x, y, w, h);
}

void app_pointer(enum app_id app, struct console *con,
                 int mx, int my, int left)
{
    if (app == APP_PAINT)
        paint_pointer(con, mx, my, left);
}

// after a full console re-render (drag, focus change): apps that paint
// pixels over their console get a chance to put them back
void app_after_paint(enum app_id app, struct console *con)
{
    if (app == APP_INTERNET)
        internet_app_repaint(con);
    else if (app == APP_FILES)
        filemgr_app_repaint(con);
    else if (app == APP_MUSIC)
        music_app_repaint(con);
    else if (app == APP_DOWNLOAD)
        download_app_repaint(con);
    else if (app == APP_UPDATE)
        update_app_repaint(con);
    else if (app == APP_SETTINGS)
        settings_app_repaint(con);
}

const char *app_name(enum app_id app)
{
    if (app == APP_SHELL)     return "Shell";
    if (app == APP_NOTEPAD)   return "Notepad";
    if (app == APP_FILES)     return "Files";
    if (app == APP_VIEWER)    return "Viewer";
    if (app == APP_RUNNER)    return "Runner";
    if (app == APP_INTERNET)  return "Internet";
    if (app == APP_PAINT)     return "Paint";
    if (app == APP_SETTINGS)  return "Settings";
    if (app == APP_MUSIC)     return "Music";
    if (app == APP_DOWNLOAD)  return "Download";
    if (app == APP_UPDATE)    return "Update";
    if (app == APP_CODE)     return "Code";
    return "?";
}

uint32_t app_accent(enum app_id app)
{
    if (app == APP_SHELL)     return 0x546E7A;
    if (app == APP_NOTEPAD)   return 0xB8860B;
    if (app == APP_FILES)     return 0x1E5AA8;
    if (app == APP_VIEWER)    return 0x78909C;
    if (app == APP_RUNNER)    return 0x78909C;
    if (app == APP_INTERNET)  return 0xE53935;
    if (app == APP_PAINT)     return 0x8E24AA;
    if (app == APP_SETTINGS)  return 0x616161;
    if (app == APP_MUSIC)     return 0xD81B60;
    if (app == APP_DOWNLOAD)  return 0x00897B;
    if (app == APP_UPDATE)    return 0x43A047;
    if (app == APP_CODE)     return 0x007ACC;
    return 0x616161;
}
