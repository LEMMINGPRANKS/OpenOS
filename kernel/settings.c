#include "settings.h"
#include "ramfs.h"
#include "store.h"
#include "term.h"
#include "kb.h"
#include "nic.h"
#include "net.h"
#include "http.h"
#include "version.h"
#include "appbar.h"

// --- the store ---------------------------------------------------------------
// key=value lines in /settings.txt (ramfs, flushed to DR1, restored at boot)

#define SET_FILE "/settings.txt"
#define SET_BUF  512

static char setbuf[SET_BUF];
static int  setloaded;

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void ensure_loaded(void)
{
    if (setloaded)
        return;
    setloaded = 1;
    setbuf[0] = 0;
    uint32_t size = 0;
    const char *data = ramfs_read(SET_FILE, &size);
    if (!data)
        return;
    if (size >= SET_BUF)
        size = SET_BUF - 1;
    for (uint32_t i = 0; i < size; i++)
        setbuf[i] = data[i];
    setbuf[size] = 0;
}

int settings_get(const char *key, char *out, int max)
{
    ensure_loaded();
    int klen = slen(key);
    char *p = setbuf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        int linelen = (int)(p - line);
        if (*p == '\n')
            p++;
        int match = linelen > klen && line[klen] == '=';
        for (int i = 0; match && i < klen; i++)
            if (line[i] != key[i])
                match = 0;
        if (match) {
            int vlen = linelen - klen - 1;
            if (vlen > max - 1)
                vlen = max - 1;
            for (int i = 0; i < vlen; i++)
                out[i] = line[klen + 1 + i];
            out[vlen] = 0;
            return 0;
        }
    }
    return -1;
}

void settings_set(const char *key, const char *value)
{
    ensure_loaded();
    int klen = slen(key);
    char nb[SET_BUF];
    int n = 0;
    char *p = setbuf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        int linelen = (int)(p - line);
        if (*p == '\n')
            p++;
        int match = linelen > klen && line[klen] == '=';
        for (int i = 0; match && i < klen; i++)
            if (line[i] != key[i])
                match = 0;
        if (match)
            continue;                   // old value: replaced below
        if (n + linelen + 1 >= SET_BUF)
            break;
        for (int i = 0; i < linelen; i++)
            nb[n++] = line[i];
        nb[n++] = '\n';
    }
    for (int i = 0; key[i] && n < SET_BUF - 1; i++)   nb[n++] = key[i];
    if (n < SET_BUF - 1) nb[n++] = '=';
    for (int i = 0; value[i] && n < SET_BUF - 1; i++) nb[n++] = value[i];
    if (n < SET_BUF - 1) nb[n++] = '\n';
    nb[n] = 0;
    for (int i = 0; i <= n; i++)
        setbuf[i] = nb[i];
    ramfs_write(SET_FILE, setbuf, (uint32_t)n);
    store_flush();                      // survives power-off, no `save` needed
}

// --- the app -----------------------------------------------------------------

#define SET_ACCENT  0x616161
#define ROW_NIC     0            // st.row index, not a screen row
#define ROW_SERVER  1
#define SRV_MAX     24

#define SCR_NIC     (APPBAR_ROWS + 2)
#define SCR_ACTIVE  (APPBAR_ROWS + 3)
#define SCR_SERVER  (APPBAR_ROWS + 5)
#define SCR_HINT    (APPBAR_ROWS + 8)
#define SCR_MSG     (APPBAR_ROWS + 9)

static struct {
    int  row;
    int  nic_sel;                       // 0 = auto, i+1 = family i
    char srv[SRV_MAX];
    int  srv_n;
    char msg[48];
} st;

#define NIC_CHOICES (nic_driver_count() + 1)

static const char *nic_choice(int i)
{
    return i == 0 ? "auto" : nic_driver_name(i - 1);
}

static void nic_pref_load(void)
{
    char v[16];
    st.nic_sel = 0;
    if (settings_get("nic", v, sizeof v) == 0)
        for (int i = 0; i < NIC_CHOICES; i++) {
            const char *a = nic_choice(i), *b = v;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == *b && !*a) { st.nic_sel = i; break; }
        }
}

// "10.0.2.2:8080" -> ip + port (dotted quad only; no DNS in a settings box)
static int parse_ip_port(const char *s, uint32_t *ip, uint16_t *port)
{
    uint32_t v = 0;
    int shift = 24, oct = 0, any = 0, dots = 0, i = 0;
    uint16_t p = 0;
    for (; s[i] && s[i] != ':'; i++) {
        if (s[i] == '.') {
            if (!any || oct > 255 || dots == 3) return -1;
            v |= (uint32_t)oct << shift;
            shift -= 8; oct = 0; any = 0; dots++;
        } else if (s[i] >= '0' && s[i] <= '9') {
            oct = oct * 10 + (s[i] - '0'); any = 1;
        } else
            return -1;
    }
    if (dots != 3 || !any || oct > 255)
        return -1;
    v |= (uint32_t)oct;
    if (s[i] == ':') {
        for (int k = i + 1; s[k]; k++) {
            if (s[k] < '0' || s[k] > '9' || p > 9999)
                return -1;
            p = (uint16_t)(p * 10 + (uint16_t)(s[k] - '0'));
        }
        if (!p)
            return -1;
    } else {
        p = 80;
    }
    *ip = v;
    *port = p;
    return 0;
}

static void setmsg(const char *m)
{
    int i = 0;
    for (; m[i] && i < (int)sizeof st.msg - 1; i++) st.msg[i] = m[i];
    st.msg[i] = 0;
}

static void apply_nic(int dir)
{
    st.nic_sel = (st.nic_sel + dir + NIC_CHOICES) % NIC_CHOICES;
    const char *fam = nic_choice(st.nic_sel);
    settings_set("nic", fam);
    http_net_forget();
    net_reset();
    nic_rebind(fam);
    // instant feedback: bring the new card up right now
    if (net_init() != 0) {
        setmsg("no card answered yet -- will retry on next use");
    } else {
        char msg[48];
        int n = 0;
        const char *pre = "up: ";
        for (int i = 0; pre[i] && n < 46; i++) msg[n++] = pre[i];
        for (int i = 0; fam[i] && n < 46; i++) msg[n++] = fam[i];
        char ipstr[16];
        net_ip_str(net_local_ip(), ipstr);
        const char *mid = "  ip ";
        for (int i = 0; mid[i] && n < 46; i++) msg[n++] = mid[i];
        for (int i = 0; ipstr[i] && n < 46; i++) msg[n++] = ipstr[i];
        msg[n] = 0;
        setmsg(msg);
    }
}

static void apply_server(void)
{
    st.srv[st.srv_n] = 0;
    uint32_t ip;
    uint16_t port;
    if (parse_ip_port(st.srv, &ip, &port) != 0) {
        setmsg("that is not an ip:port (e.g. 10.0.2.2:8080)");
        return;
    }
    http_set_server(ip, port);
    settings_set("server", st.srv);
    setmsg("server saved");
}

static void settings_draw(struct console *con)
{
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    term_clear();
    appbar_paint(con, "Settings", OS_VERSION, SET_ACCENT);

    term_goto(con, 0, SCR_NIC);
    term_setcolor(st.row == ROW_NIC ? (uint8_t)(0x70 | 0x0F) : 0x07);
    term_puts("  network card   < ");
    term_puts(nic_choice(st.nic_sel));
    term_puts(" >");

    term_goto(con, 0, SCR_ACTIVE);
    term_setcolor(0x08);
    term_puts("  active: ");
    term_puts(nic_name());

    term_goto(con, 0, SCR_SERVER);
    term_setcolor(st.row == ROW_SERVER ? (uint8_t)(0x70 | 0x0F) : 0x07);
    term_puts("  package server [");
    term_puts(st.srv);
    if (st.row == ROW_SERVER)
        term_putc('_');
    term_puts("]");

    term_goto(con, 0, SCR_HINT);
    term_setcolor(0x07);
    term_puts("up/down pick a row   left/right or n changes the card");
    term_goto(con, 0, SCR_HINT + 1);
    term_puts("enter applies the server   esc closes");

    term_goto(con, 0, SCR_MSG);
    term_setcolor(0x02);
    if (st.msg[0])
        term_puts(st.msg);
}

void settings_app_repaint(struct console *con)
{
    settings_draw(con);
}

void settings_app_open(struct console *con)
{
    st.row = ROW_NIC;
    st.msg[0] = 0;
    nic_pref_load();
    if (settings_get("server", st.srv, SRV_MAX) != 0) {
        net_ip_str(http_server_ip(), st.srv);
        int pn = 0;
        char ports[8];
        int port = http_server_port();
        if (!port) ports[pn++] = '0';
        while (port) { ports[pn++] = (char)('0' + port % 10); port /= 10; }
        int base = slen(st.srv);
        st.srv[base++] = ':';
        while (pn) st.srv[base++] = ports[--pn];
        st.srv[base] = 0;
    }
    st.srv_n = slen(st.srv);
    settings_draw(con);
}

void settings_app_input(struct console *con, char c)
{
    if (c == KEY_UP) {
        st.row = st.row == ROW_NIC ? ROW_SERVER : ROW_NIC;
    } else if (c == KEY_DOWN) {
        st.row = st.row == ROW_NIC ? ROW_SERVER : ROW_NIC;
    } else if (st.row == ROW_NIC &&
               (c == KEY_LEFT || c == KEY_RIGHT || c == 'n')) {
        apply_nic(c == KEY_LEFT ? -1 : 1);
    } else if (st.row == ROW_SERVER) {
        if (c == '\n') {
            apply_server();
        } else if (c == '\b') {
            if (st.srv_n)
                st.srv_n--;
        } else if (c >= 32 && c < 127 && st.srv_n < SRV_MAX - 1) {
            st.srv[st.srv_n++] = c;
        }
    }
    settings_draw(con);
}
