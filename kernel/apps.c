#include "apps.h"
#include "shell.h"
#include "filemgr.h"
#include "ramfs.h"
#include "files.h"
#include "ext.h"
#include "term.h"
#include "wm.h"
#include "js.h"
#include "browser.h"
#include "news.h"
#include "http.h"

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

static char app_arg[32];              // filename for the viewer

void app_set_arg(const char *arg)
{
    for (int i = 0; i < 31 && arg[i]; i++)
        app_arg[i] = arg[i];
    app_arg[31] = 0;
}

static void viewer_open(struct console *con)
{
    term_use(con);
    const struct ext_type *t = ext_lookup(app_arg);
    term_setcolor(t->vga_color);
    term_puts("--- ");
    term_puts(app_arg);
    term_puts(" ---\n");
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
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
    term_puts("--- run ");
    term_puts(app_arg);
    term_puts(" ---\n\n");
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

// --- Browser: a demo web browser with a typeable address bar ------------
//
// Type an address, press Enter: "10.0.2.2:8080/news" fetches over our own
// TCP stack, anything else is opened as a local file (demo.html).

#define BURL_MAX 96
#define BURL_PROMPT "open: "

static char burl[BURL_MAX];             // the address bar
static int burl_n;

static void burl_draw(struct console *con)
{
    term_use(con);
    term_goto(con, 0, 0);
    term_setcolor(0x1B);                // light cyan
    term_puts(BURL_PROMPT);
    term_puts(burl_n ? burl : "(type an address, Enter loads)");
    int used = (int)sizeof(BURL_PROMPT) - 1 + (burl_n ? burl_n : 31);
    for (int i = used; i < (int)term_cols(con) - 2; i++)
        term_putc(' ');
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
}

// "a.b.c.d" of exactly n chars -> host-order IP, else 0
static uint32_t ip_parse(const char *s, int n)
{
    uint32_t ip = 0;
    int i = 0;
    for (int part = 0; part < 4; part++) {
        if (i >= n || s[i] < '0' || s[i] > '9')
            return 0;
        uint32_t v = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (uint32_t)(s[i++] - '0');
            if (v > 255)
                return 0;
        }
        ip = (ip << 8) | v;
        if (part < 3) {
            if (i >= n || s[i] != '.')
                return 0;
            i++;
        }
    }
    return i == n ? ip : 0;
}

static void browser_load(struct console *con)
{
    char u[BURL_MAX];
    int un = 0;
    for (int i = 0; i < burl_n && un < BURL_MAX - 1; i++)
        u[un++] = burl[i];
    u[un] = 0;
    if (un > 7 && u[0]=='h' && u[1]=='t' && u[2]=='t' && u[3]=='p' &&
        u[4]==':' && u[5]=='/' && u[6]=='/') {
        for (int i = 0; i <= un - 7; i++)
            u[i] = u[i + 7];
        un -= 7;
    }

    // split "host[:port]/path"
    char host[80];
    int hn = 0;
    int slash = -1;
    for (int i = 0; i < un; i++) {
        if (u[i] == '/') { slash = i; break; }
        if (hn < 79) host[hn++] = u[i];
    }
    const char *path = slash >= 0 ? u + slash : "/";
    uint16_t port = 8080;
    for (int i = 0; i < hn; i++)
        if (host[i] == ':') {
            uint32_t v = 0;
            for (int k = i + 1; k < hn; k++)
                v = v * 10 + (uint32_t)(host[k] - '0');
            if (v) port = (uint16_t)v;
            hn = i;
            break;
        }

    uint32_t ip = ip_parse(host, hn);
    if (ip) {                            // a real web address!
        burl_draw(con);
        term_goto(con, 0, 1);
        term_puts("loading http://");
        for (int i = 0; i < hn; i++)
            term_putc(host[i]);
        term_puts("...\n");
        http_set_server(ip, port);
        static uint8_t body[HTTP_MAX];
        if (http_ensure_net() == 0) {
            int n = http_get(path, body, HTTP_MAX);
            if (n > 0) {
                browser_render(con, (const char *)body, (uint32_t)n, burl);
                return;
            }
            term_puts("(the server did not answer with a page)\n");
            return;
        }
        return;                          // http_ensure_net said why
    }

    // not an IP: treat it as a local file path
    char fpath[BURL_MAX];
    int fn = 0;
    if (path[0] != '/')
        fpath[fn++] = '/';
    for (int i = 0; path[i] && fn < BURL_MAX - 1; i++)
        fpath[fn++] = path[i];
    fpath[fn] = 0;
    uint32_t size = 0;
    const char *data = files_read(fpath, &size);
    if (!data) {
        burl_draw(con);
        term_goto(con, 0, 1);
        term_puts("(not found: ");
        term_puts(fpath);
        term_puts(")\ntry 10.0.2.2:8080/news or demo.html\n");
        return;
    }
    browser_render(con, data, size, burl);
}

static void browser_input(struct console *con, char c)
{
    if (c == '\n') {
        browser_load(con);
        return;
    }
    if (c == '\b') {
        if (burl_n)
            burl[--burl_n] = 0;
    } else if (c >= 32 && c < 127 && burl_n < BURL_MAX - 1) {
        burl[burl_n++] = c;
        burl[burl_n] = 0;
    } else {
        return;
    }
    burl_draw(con);
}

static void browser_open(struct console *con)
{
    burl_n = 0;
    burl[0] = 0;
    if (app_arg[0]) {                    // opened from a double-click
        for (int i = 0; app_arg[i] && burl_n < BURL_MAX - 1; i++)
            burl[burl_n++] = app_arg[i];
        burl[burl_n] = 0;
    }
    browser_load(con);
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
    if (ends_with(path, ".html"))
        return wm_open(APP_BROWSER, 110, 80, OPEN_WIN_W, 424);
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
    term_puts("NOTEPAD -- typing is saved to note.txt\n");
    term_puts("(the shell can read it: cat note.txt)\n\n");
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
    else if (app == APP_BROWSER)
        browser_open(con);
    else if (app == APP_NEWS)
        news_fetch(con);
}

void app_input(enum app_id app, struct console *con, char c)
{
    if (app == APP_SHELL)
        shell_app_input(con, c);
    else if (app == APP_NOTEPAD)
        notepad_input(con, c);
    else if (app == APP_FILES)
        filemgr_input(con, c);
    else if (app == APP_BROWSER)
        browser_input(con, c);
    (void)con; (void)c;                // other apps take no keyboard input
}

void app_click(enum app_id app, struct console *con, int mx, int my, int dbl)
{
    if (app == APP_FILES)
        filemgr_click(con, mx, my, dbl);
    (void)mx; (void)my; (void)dbl;
}

const char *app_name(enum app_id app)
{
    if (app == APP_SHELL)   return "Shell";
    if (app == APP_NOTEPAD) return "Notepad";
    if (app == APP_FILES)   return "Files";
    if (app == APP_VIEWER)  return "Viewer";
    if (app == APP_RUNNER)  return "Runner";
    if (app == APP_BROWSER) return "Browser";
    if (app == APP_NEWS)    return "News";
    return "?";
}
