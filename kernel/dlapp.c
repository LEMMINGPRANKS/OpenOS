#include "dlapp.h"
#include "http.h"
#include "kb.h"
#include "ramfs.h"
#include "store.h"
#include "term.h"
#include "timer.h"
#include "appbar.h"
#include "gfx.h"
#include "font.h"

// The Download app. Fetches the server's /index, shows one package per
// row (grey selection band like Files), arrow keys + Enter (or a click)
// installs. 'r' refreshes.

#define DL_MAX 16
#define DL_NAME 28

#define DL_ACCENT 0x00897B
#define LIST_TOP  (APPBAR_ROWS + 1)

static char pkgs[DL_MAX][DL_NAME];    // first word of each index line
static char lines[DL_MAX][60];        // the full line to show
static int pkg_count;
static int sel;
static uint8_t body_mem[HTTP_MAX];    // reuse across fetches
static struct console *con_app;

static int status_row(void)
{
    return LIST_TOP + (pkg_count > 0 ? pkg_count : 1) + 1;
}

static void say(const char *s)
{
    term_use(con_app);
    term_goto(con_app, 0, status_row());
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    // wipe the old status line, then print the new one
    for (int i = 0; i + 1 < (int)term_cols(con_app); i++)
        term_putc(' ');
    term_goto(con_app, 0, status_row());
    term_puts(s);
}

static void trim_crlf(char *s)
{
    int n = 0;
    while (s[n] && s[n] != '\r' && s[n] != '\n')
        n++;
    s[n] = 0;
}

static void fetch_list(void)
{
    pkg_count = 0;
    sel = 0;
    say("fetching the package list...");
    int n = http_get("/index", body_mem, HTTP_MAX);
    if (n <= 0) {
        say("could not reach the server (is it running?)");
        return;
    }
    int i = 0;
    while (i < n && pkg_count < DL_MAX) {
        // one package per line
        int start = i;
        while (i < n && body_mem[i] != '\n')
            i++;
        int len = i - start;
        if (len > 0 && len < (int)sizeof lines[0]) {
            for (int k = 0; k < len; k++)
                lines[pkg_count][k] = (char)body_mem[start + k];
            lines[pkg_count][len] = 0;
            trim_crlf(lines[pkg_count]);
            // first word = package name
            int w = 0;
            while (lines[pkg_count][w] && lines[pkg_count][w] != ' ' &&
                   w < DL_NAME - 1)
                w++;
            for (int k = 0; k < w; k++)
                pkgs[pkg_count][k] = lines[pkg_count][k];
            pkgs[pkg_count][w] = 0;
            if (pkgs[pkg_count][0])
                pkg_count++;
        }
        i++;
    }
}

static void install(int idx)
{
    char msg[80];
    int m = 0;
    const char *pre = "downloading ";
    for (int i = 0; pre[i] && m < 78; i++) msg[m++] = pre[i];
    for (int i = 0; pkgs[idx][i] && m < 78; i++) msg[m++] = pkgs[idx][i];
    const char *post = "...";
    for (int i = 0; post[i] && m < 78; i++) msg[m++] = post[i];
    msg[m] = 0;
    say(msg);
    char path[DL_NAME + 2];
    path[0] = '/';
    int n = 0;
    while (pkgs[idx][n]) {
        path[1 + n] = pkgs[idx][n];
        n++;
    }
    path[1 + n] = 0;
    int got = http_get(path, body_mem, HTTP_MAX);
    if (got == -2) {
        say("no such package on the server");
        return;
    }
    if (got < 0) {
        say("download failed");
        return;
    }
    if (ramfs_write(path, (const char *)body_mem, (uint32_t)got) != 0) {
        say("ramfs full");
        return;
    }
    m = 0;
    const char *ok1 = "installed ";
    for (int i = 0; ok1[i] && m < 78; i++) msg[m++] = ok1[i];
    for (int i = 0; pkgs[idx][i] && m < 78; i++) msg[m++] = pkgs[idx][i];
    const char *ok2 = " -- find it in Files";
    for (int i = 0; ok2[i] && m < 78; i++) msg[m++] = ok2[i];
    msg[m] = 0;
    say(msg);
    store_flush();                     // survives power-off straight away
}

static void paint(void)
{
    term_use(con_app);
    term_protect(con_app, APPBAR_ROWS);
    term_clear();
    appbar_paint(con_app, "Download", "spgk packages", DL_ACCENT);

    term_goto(con_app, 0, LIST_TOP);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    if (!pkg_count) {
        term_puts("  (nothing fetched yet -- press r)");
    }
    for (int i = 0; i < pkg_count; i++) {
        term_goto(con_app, 0, LIST_TOP + i);
        term_setcolor(i == sel ? (uint8_t)(0x70 | 0x0F) : 0x07);
        term_puts("      ");            // room for the pixel icon
        term_puts(lines[i]);
    }
    term_goto(con_app, 0, status_row());
    term_setcolor(0x07);
    term_puts("up/down pick   enter install   r refresh");

    // pixel icons: a little teal disc per row
    uint32_t vx, vy, vw, vh;
    term_view(con_app, &vx, &vy, &vw, &vh);
    (void)vw; (void)vh;
    for (int i = 0; i < pkg_count; i++)
        gfx_fill_circle(vx + 14, vy + (uint32_t)(LIST_TOP + i) * FONT_H + 8,
                        5, DL_ACCENT);
}

void download_app_open(struct console *con)
{
    con_app = con;
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    paint();
    fetch_list();
    paint();
}

void download_app_repaint(struct console *con)
{
    if (con == con_app)
        paint();
}

void download_app_input(struct console *con, char c)
{
    con_app = con;
    if (c == 'r') {
        fetch_list();
        paint();
    } else if (c == KEY_UP && sel > 0) {
        sel--;
        paint();
    } else if (c == KEY_DOWN && sel + 1 < pkg_count) {
        sel++;
        paint();
    } else if (c == '\n' && pkg_count) {
        install(sel);
    }
}

void download_app_click(struct console *con, int mx, int my)
{
    con_app = con;
    int col, row;
    if (term_locate(con, mx, my, &col, &row) != 0)
        return;
    int idx = row - LIST_TOP;
    if (idx >= 0 && idx < pkg_count) {
        sel = idx;
        paint();
        install(idx);
    }
}
