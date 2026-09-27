#include "updapp.h"
#include "kupdate.h"
#include "http.h"
#include "kb.h"
#include "term.h"
#include "version.h"
#include "portio.h"
#include "appbar.h"
#include "gfx.h"
#include "font.h"

// The Update app. The kernel can replace itself: kupdate downloads into
// the other A/B slot and stages it; the reboot lands in the new kernel
// (and a kernel that never confirms itself rolls back automatically).

#define VER_MAX 12
#define VER_STR 12

#define UPD_ACCENT 0x43A047
#define LIST_TOP  (APPBAR_ROWS + 3)

static char vers[VER_MAX][VER_STR];   // "1.4.1"
static char vflag[VER_MAX][10];       // "stable", "unstable", ...
static int ver_count;
static struct console *con_app;

static int status_row(void)
{
    return LIST_TOP + (ver_count > 0 ? ver_count : 1) + 1;
}

static void say(const char *s)
{
    term_use(con_app);
    term_goto(con_app, 0, status_row());
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    for (int i = 0; i + 1 < (int)term_cols(con_app); i++)
        term_putc(' ');
    term_goto(con_app, 0, status_row());
    term_puts(s);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void paint(void)
{
    term_use(con_app);
    term_protect(con_app, APPBAR_ROWS);
    term_clear();
    appbar_paint(con_app, "Update", "kernel A/B updates", UPD_ACCENT);

    term_goto(con_app, 0, LIST_TOP - 2);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("running ");
    term_setcolor(0x02);
    term_puts(OS_VERSION);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("  slot ");
    term_putc((char)('A' + kupdate_boot_slot()));
    if (kupdate_was_candidate())
        term_puts("  -- just updated, confirmed itself");

    term_goto(con_app, 0, LIST_TOP);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    if (!ver_count)
        term_puts("  (press l for every kernel version)");
    for (int i = 0; i < ver_count; i++) {
        int running = str_eq(vers[i], OS_VERSION);
        term_goto(con_app, 0, LIST_TOP + i);
        term_setcolor(running ? (uint8_t)(0x70 | 0x02) : 0x07);
        term_puts("      ");            // room for the pixel icon
        term_putc((char)('1' + i));
        term_puts("  ");
        term_puts(vers[i]);
        term_puts("  ");
        term_puts(running ? "(running)" : vflag[i]);
    }

    term_goto(con_app, 0, status_row() + 1);
    term_setcolor(0x07);
    term_puts("c check   i install stable   l list   1-9 install that one");

    // pixel icons: green disc = stable, grey = unstable, ring = running
    uint32_t vx, vy, vw, vh;
    term_view(con_app, &vx, &vy, &vw, &vh);
    (void)vw; (void)vh;
    for (int i = 0; i < ver_count; i++) {
        int running = str_eq(vers[i], OS_VERSION);
        uint32_t cy = vy + (uint32_t)(LIST_TOP + i) * FONT_H + 8;
        uint32_t col = str_eq(vflag[i], "stable") ? UPD_ACCENT : 0x9AA0A6;
        gfx_fill_circle(vx + 14, cy, 5, col);
        if (running)                    // white centre dot marks this boot
            gfx_fill_circle(vx + 14, cy, 2, 0xFFFFFF);
    }
}

static void do_check(void)
{
    say("checking the stable channel...");
    char ver[VER_STR];
    uint32_t size, entry, bss;
    int r = kupdate_check(0, ver, sizeof ver, &size, &entry, &bss);
    if (r == 1) {
        say("you are on the latest stable kernel");
        return;
    }
    if (r != 0) {
        say("could not reach the server (is it running?)");
        return;
    }
    char msg[60];
    int m = 0;
    const char *pre = "new kernel: ";
    for (int i = 0; pre[i] && m < 58; i++) msg[m++] = pre[i];
    for (int i = 0; ver[i] && m < 58; i++) msg[m++] = ver[i];
    const char *post = "  (i installs)";
    for (int i = 0; post[i] && m < 58; i++) msg[m++] = post[i];
    msg[m] = 0;
    say(msg);
}

static void fetch_versions(void)
{
    ver_count = 0;
    static uint8_t body[HTTP_MAX];
    say("fetching the version list...");
    int n = http_get("/kernel/versions", body, HTTP_MAX);
    if (n <= 0) {
        say("could not reach the server (is it running?)");
        return;
    }
    int i = 0;
    while (i < n && ver_count < VER_MAX) {
        int start = i;
        while (i < n && body[i] != '\n')
            i++;
        int len = i - start;
        if (len > 0 && len < 40) {
            // "1.4.1 stable" -> version word + rest
            int w = 0;
            while (start + w < i && body[start + w] != ' ' && w < VER_STR - 1) {
                vers[ver_count][w] = (char)body[start + w];
                w++;
            }
            vers[ver_count][w] = 0;
            int f = 0;
            for (int k = w + 1; k < len && f < 9; k++)
                vflag[ver_count][f++] = (char)body[start + k];
            vflag[ver_count][f] = 0;
            if (vers[ver_count][0])
                ver_count++;
        }
        i++;
    }
}

static void do_install(const char *ver)
{
    char msg[60];
    int m = 0;
    const char *pre = "installing kernel ";
    for (int i = 0; pre[i] && m < 58; i++) msg[m++] = pre[i];
    for (int i = 0; ver[i] && m < 58; i++) msg[m++] = ver[i];
    const char *post = "...";
    for (int i = 0; post[i] && m < 58; i++) msg[m++] = post[i];
    msg[m] = 0;
    say(msg);
    if (kupdate_install(ver) != 0) {
        say("install failed (server reachable? version real?)");
        return;
    }
    say("staged! rebooting...");
    outb(0x64, 0xFE);                   // keyboard-controller reset
    for (;;)
        __asm__ volatile ("hlt");
}

void update_app_open(struct console *con)
{
    con_app = con;
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    paint();
}

void update_app_repaint(struct console *con)
{
    if (con == con_app)
        paint();
}

void update_app_input(struct console *con, char c)
{
    con_app = con;
    if (c == 'c') {
        do_check();
    } else if (c == 'i') {
        do_install("stable");
    } else if (c == 'l') {
        fetch_versions();
        paint();
    } else if (c >= '1' && c <= '9') {
        int idx = c - '1';
        if (idx < ver_count)
            do_install(vers[idx]);
    }
}
