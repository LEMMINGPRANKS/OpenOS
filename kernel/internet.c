#include "internet.h"
#include "browser.h"
#include "ramfs.h"
#include "files.h"
#include "http.h"
#include "net.h"
#include "desktop.h"
#include "store.h"
#include "kupdate.h"
#include "version.h"
#include "apps.h"

// The Internet app. Five tabs on row 0, a context line on row 1 (the
// Browser's address bar or the Comments/Ideas post box), pages rendered
// by browser.c underneath.

#define TAB_BROWSER   0
#define TAB_NEWS      1
#define TAB_UPDATES   2
#define TAB_COMMENTS  3
#define TAB_IDEAS     4
#define TAB_COUNT     5

static const char *tab_names[TAB_COUNT] = {
    "Browser", "News", "Updates", "Comments", "Ideas"
};

#define LINE_MAX_CHARS 96
static int tab;
static char burl[LINE_MAX_CHARS];          // Browser address bar
static int burl_n;
static char cmsg[LINE_MAX_CHARS];          // Comments/Ideas post box
static int cmsg_n;

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int ends_with(const char *s, const char *suf)
{
    int n = str_len(s), m = str_len(suf);
    if (m > n)
        return 0;
    for (int i = 0; i < m; i++)
        if (s[n - m + i] != suf[i])
            return 0;
    return 1;
}

static int starts_with(const char *s, const char *pre)
{
    for (int i = 0; pre[i]; i++)
        if (s[i] != pre[i])
            return 0;
    return 1;
}

// the server calls the news page "/news" (no .html), so the address alone
// can't decide render-vs-download: look at the actual bytes instead.
static int looks_like_text(const uint8_t *body, uint32_t n)
{
    uint32_t lim = n < 256 ? n : 256;
    for (uint32_t i = 0; i < lim; i++) {
        uint8_t c = body[i];
        if (c < 32 && c != '\n' && c != '\r' && c != '\t')
            return 0;
    }
    return 1;
}

static int looks_like_html(const uint8_t *body, uint32_t n)
{
    static const char *tags[] = { "!doctype", "html", "head", "body", 0 };
    uint32_t lim = n < 256 ? n : 256;
    for (int t = 0; tags[t]; t++) {
        const char *tag = tags[t];
        int tn = str_len(tag);
        for (uint32_t i = 0; i + tn + 1 < lim; i++) {
            if (body[i] != '<')
                continue;
            int same = 1;
            for (int k = 0; k < tn; k++) {
                char c = (char)body[i + 1 + k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != tag[k]) { same = 0; break; }
            }
            if (same)
                return 1;
        }
    }
    return 0;
}

// --- chrome: tab bar (row 0) + context line (row 1) ------------------------

#define COL_ACTIVE   0x1E      // yellow on blue
#define COL_IDLE     0x1B      // light cyan
#define COL_INPUT    0x1A      // light green
#define COL_TEXT     0x1F

static void tabs_draw(struct console *con)
{
    term_use(con);
    term_goto(con, 0, 0);
    int c = 1;
    for (int i = 0; i < TAB_COUNT; i++) {
        term_setcolor(i == tab ? COL_ACTIVE : COL_IDLE);
        term_putc('[');
        term_puts(tab_names[i]);
        term_putc(']');
        term_putc(' ');
        c += str_len(tab_names[i]) + 4;
    }
    term_setcolor(COL_TEXT);
    while (c++ < (int)term_cols(con))
        term_putc(' ');
}

static void context_draw(struct console *con)
{
    term_goto(con, 0, 1);
    if (tab == TAB_BROWSER) {
        term_setcolor(COL_INPUT);
        term_puts("open: ");
        term_puts(burl_n ? burl : "(type an address, Enter loads)");
    } else if (tab == TAB_COMMENTS || tab == TAB_IDEAS) {
        term_setcolor(COL_INPUT);
        term_puts(tab == TAB_COMMENTS ? "say: " : "idea: ");
        term_puts(cmsg_n ? cmsg : "(type, Enter posts it)");
    } else if (tab == TAB_NEWS) {
        term_setcolor(COL_TEXT);
        term_puts("the latest from the OpenOS server (r = refresh)");
    } else {
        term_setcolor(COL_TEXT);
        term_puts("u check  i install kernel  r reboot");
    }
    term_putc('\n');
    term_setcolor(COL_TEXT);
}

static void chrome_draw(struct console *con)
{
    tabs_draw(con);
    context_draw(con);
}

static int tab_at(int col)
{
    int c = 1;
    for (int i = 0; i < TAB_COUNT; i++) {
        int w = str_len(tab_names[i]) + 2;
        if (col >= c && col < c + w)
            return i;
        c += w + 1;
    }
    return -1;
}

// page + chrome on top (browser_render clears, so chrome goes last)
static void page_draw(struct console *con, const char *html, uint32_t n)
{
    browser_render(con, html, n, 0);
    chrome_draw(con);
}

// --- fetching ---------------------------------------------------------------

static void set_line(char *dst, int *n, const char *s)
{
    int k = 0;
    while (s[k] && k < LINE_MAX_CHARS - 1) {
        dst[k] = s[k];
        k++;
    }
    dst[k] = 0;
    *n = k;
}

// a downloaded non-HTML file: install into ramfs so it shows up as a
// brand-new desktop icon -- this IS the updater's "grab a new feature"
static void net_install(struct console *con, const char *path,
                        const uint8_t *body, uint32_t n)
{
    const char *nm = path;
    for (const char *q = path; *q; q++)
        if (*q == '/')
            nm = q + 1;
    char fpath[80];
    int fn = 0;
    fpath[fn++] = '/';
    for (int i = 0; nm[i] && fn < 78; i++)
        fpath[fn++] = nm[i];
    fpath[fn] = 0;
    if (ramfs_write(fpath, (const char *)body, n) != 0) {
        chrome_draw(con);
        term_puts("(ramfs full -- could not install)\n");
        return;
    }
    static char msg[256];
    int m = 0;
    const char *parts[] = {
        "<html><body><h1>Installed!</h1><p>", nm,
        " is on the desktop now. Double-click its icon to run it.</p>"
        "<p><a href='/updates'>more updates</a></p></body></html>"
    };
    for (int i = 0; i < 3; i++)
        for (const char *p = parts[i]; *p && m < 254; p++)
            msg[m++] = *p;
    msg[m] = 0;
    page_draw(con, msg, (uint32_t)m);
    desktop_repaint();                   // new icon appears immediately
    store_flush();                       // and it survives reboot (DR1)
}

// what to do with a fetched body: render text/HTML, install the rest
static void net_got(struct console *con, const char *path,
                    uint8_t *body, int n)
{
    if (n <= 0) {
        chrome_draw(con);
        term_puts("(the server did not answer with a page)\n");
        return;
    }
    if (starts_with(path, "/updates/")) {
        net_install(con, path, body, (uint32_t)n);
        return;
    }
    if (ends_with(path, ".html") || ends_with(path, ".htm") ||
        looks_like_html(body, (uint32_t)n) || looks_like_text(body, (uint32_t)n)) {
        page_draw(con, (const char *)body, (uint32_t)n);
        return;
    }
    net_install(con, path, body, (uint32_t)n);
}

// a path on OUR package server (news, comments, updates...)
static void server_page(struct console *con, const char *path)
{
    static uint8_t body[HTTP_MAX];
    if (http_ensure_net() != 0)
        return;
    net_got(con, path, body, http_get(path, body, HTTP_MAX));
}

static void browser_load(struct console *con)
{
    char u[LINE_MAX_CHARS];
    int un = burl_n;
    for (int i = 0; i < burl_n; i++)
        u[i] = burl[i];
    u[un] = 0;
    if (un > 7 && starts_with(u, "http://")) {
        for (int i = 0; i <= un - 7; i++)
            u[i] = u[i + 7];
        un -= 7;
    }

    // "host..." with a dot or port in it = a real address (name OR IP)
    int host_end = 0;
    while (u[host_end] && u[host_end] != '/')
        host_end++;
    int remote = 0;
    for (int i = 0; i < host_end; i++)
        if (u[i] == '.' || u[i] == ':')
            remote = 1;

    if (remote) {
        chrome_draw(con);
        term_puts("loading ");
        term_puts(u);
        term_puts("...\n");
        static uint8_t body[HTTP_MAX];
        int n = http_get_url(u, body, HTTP_MAX);
        net_got(con, u, body, n);
        return;
    }

    // bare name: local file first
    char fpath[LINE_MAX_CHARS];
    int fn = 0;
    if (u[0] != '/')
        fpath[fn++] = '/';
    for (int i = 0; u[i] && fn < LINE_MAX_CHARS - 1; i++)
        fpath[fn++] = u[i];
    fpath[fn] = 0;
    uint32_t size = 0;
    const char *data = files_read(fpath, &size);
    if (data) {
        page_draw(con, data, size);
        return;
    }
    // then a page on the package server, so "news" just gets the news
    server_page(con, fpath);
}

static void news_tab(struct console *con)
{
    server_page(con, "/news");
}

static void post_line(struct console *con, const char *path)
{
    if (!cmsg_n) {
        chrome_draw(con);
        term_puts("(type something first)\n");
        return;
    }
    cmsg[cmsg_n] = 0;
    static uint8_t body[HTTP_MAX];
    if (http_ensure_net() != 0)
        return;
    int n = http_post(path, cmsg, body, HTTP_MAX);
    if (n > 0) {
        page_draw(con, (const char *)body, (uint32_t)n);
        set_line(cmsg, &cmsg_n, "");
        chrome_draw(con);
    } else {
        chrome_draw(con);
        term_puts("(posting failed -- is the server running?)\n");
    }
}

// --- Update Manager (the Updates tab) ---------------------------------------

static void upd_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(val), "d"(port));
}

static void updates_show(struct console *con)
{
    term_use(con);
    term_clear();
    term_setcolor(COL_TEXT);
    term_puts("\n\n  running: OpenOS ");
    term_puts(OS_VERSION " (kernel slot ");
    term_putc('A' + kupdate_boot_slot());
    term_puts(")\n\n");
    term_puts("  u  check the update server (stable channel)\n");
    term_puts("  l  list every kernel version (stable + unstable)\n");
    term_puts("  i  download + install the new kernel\n");
    term_puts("  r  reboot (activates a staged update)\n");
    chrome_draw(con);
}

static void switch_tab(struct console *con, int t);

static void updates_key(struct console *con, char c)
{
    term_use(con);
    if (c == 'u' || c == 'U') {
        term_puts("checking the update server...\n");
        char ver[24];
        uint32_t size = 0, entry = 0, bss = 0;
        int r = kupdate_check(0, ver, sizeof ver, &size, &entry, &bss);
        if (r == 1)
            term_puts("kernel is up to date (" OS_VERSION ")\n");
        else if (r != 0)
            term_puts("could not reach the update server\n");
        else {
            term_puts("new kernel: ");
            term_puts(ver);
            term_puts(" (");
            char d[12];
            int dn = 0;
            if (!size) d[dn++] = '0';
            uint32_t v = size;
            while (v) { d[dn++] = (char)('0' + v % 10); v /= 10; }
            while (dn) term_putc(d[--dn]);
            term_puts(" bytes)\npress i to install it\n");
        }
    } else if (c == 'i' || c == 'I') {
        kupdate_install(0);
    } else if (c == 'l' || c == 'L') {
        term_puts("checking the update server...\n");
        kupdate_list();
    } else if (c == 'r' || c == 'R') {
        term_puts("rebooting...\n");
        upd_outb(0x64, 0xFE);            // 8042 pulse reset line
        for (;;)
            __asm__ volatile ("hlt");
    }
}

// --- app entry points --------------------------------------------------------

void internet_app_open(struct console *con)
{
    tab = TAB_BROWSER;
    set_line(cmsg, &cmsg_n, "");
    const char *arg = app_get_arg();
    if (arg && arg[0])
        set_line(burl, &burl_n, arg);
    else
        set_line(burl, &burl_n, "demo.html");
    browser_load(con);
}

void internet_app_input(struct console *con, char c)
{
    if (c == '\t') {                    // Tab cycles through the tabs
        switch_tab(con, (tab + 1) % TAB_COUNT);
        return;
    }
    if (tab == TAB_UPDATES) {
        updates_key(con, c);
        return;
    }
    if (c == '\n') {
        if (tab == TAB_BROWSER)
            browser_load(con);
        else if (tab == TAB_COMMENTS)
            post_line(con, "/comments");
        else if (tab == TAB_IDEAS)
            post_line(con, "/roadmap");
        else if (tab == TAB_NEWS)
            news_tab(con);
        return;
    }
    // r refreshes the news
    if (tab == TAB_NEWS && (c == 'r' || c == 'R')) {
        news_tab(con);
        return;
    }

    char *line = tab == TAB_BROWSER ? burl : cmsg;
    int *n = tab == TAB_BROWSER ? &burl_n : &cmsg_n;
    if (c == '\b') {
        if (*n)
            line[--(*n)] = 0;
    } else if (c >= 32 && c < 127 && *n < LINE_MAX_CHARS - 1) {
        line[(*n)++] = c;
        line[*n] = 0;
    } else {
        return;
    }
    tabs_draw(con);
    context_draw(con);
}

static void switch_tab(struct console *con, int t)
{
    tab = t;
    set_line(cmsg, &cmsg_n, "");
    term_use(con);
    term_clear();
    chrome_draw(con);
    if (tab == TAB_BROWSER)
        browser_load(con);
    else if (tab == TAB_NEWS)
        news_tab(con);
    else if (tab == TAB_UPDATES)
        updates_show(con);
    else if (tab == TAB_COMMENTS)
        server_page(con, "/comments");
    else if (tab == TAB_IDEAS)
        server_page(con, "/roadmap");
}

void internet_app_click(struct console *con, int mx, int my)
{
    int col, row;
    if (!term_locate(con, mx, my, &col, &row))
        return;
    if (row == 0) {
        int t = tab_at(col);
        if (t >= 0 && t != tab)
            switch_tab(con, t);
        return;
    }
    // a link in the page: navigate the Browser tab with it
    if (tab == TAB_BROWSER && row >= 2) {
        char href[96];
        if (!browser_link_at(con, mx, my, href, sizeof href))
            return;
        if (href[0] == '/') {            // relative: our update server
            char addr[112];
            char ip[16];
            net_ip_str(http_server_ip(), ip);
            uint32_t pv = http_server_port();
            if (!pv) pv = 8080;
            int k = 0;
            for (int i = 0; ip[i] && k < 110; i++)  addr[k++] = ip[i];
            addr[k++] = ':';
            char pd[8];
            int pn = 0;
            do { pd[pn++] = (char)('0' + pv % 10); pv /= 10; } while (pv && pn < 7);
            while (pn) addr[k++] = pd[--pn];
            for (int i = 0; href[i] && k < 110; i++) addr[k++] = href[i];
            addr[k] = 0;
            set_line(burl, &burl_n, addr);
        } else {
            set_line(burl, &burl_n, href);
        }
        browser_load(con);
    }
}
