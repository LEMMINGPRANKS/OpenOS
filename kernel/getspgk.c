#include "getspgk.h"
#include "net.h"
#include "tcp.h"
#include "ramfs.h"
#include "term.h"
#include "timer.h"

#define SPGK_SERVER_IP   IP(10,0,2,2)   // QEMU user-net gateway = host
#define SPGK_SERVER_PORT 8080
#define HTTP_MAX         8192

static int net_ready;
static uint32_t server_ip = SPGK_SERVER_IP;   // change with: getspgk server <ip>
static uint16_t server_port = SPGK_SERVER_PORT;

// "192.168.1.20" -> host-order IP; returns 0 on a bad address
static uint32_t parse_ip(const char *s)
{
    uint32_t ip = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9')
            return 0;
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (uint32_t)(*s++ - '0');
            if (v > 255)
                return 0;
        }
        ip = (ip << 8) | v;
        if (part < 3) {
            if (*s != '.')
                return 0;
            s++;
        }
    }
    return ip;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// returns 0 if a starts with b
static int str_starts(const char *a, const char *b)
{
    while (*b) {
        if (*a++ != *b++)
            return 1;
    }
    return 0;
}

static void print_dec(uint32_t v)
{
    char digits[12];
    int n = 0;
    if (!v) digits[n++] = '0';
    while (v) { digits[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) term_putc(digits[--n]);
}

static int ensure_net(void)
{
    if (net_ready)
        return 0;
    if (!net_up()) {
        term_puts("bringing up the network (e1000 + DHCP)...\n");
        int r = net_init();
        if (r == -1) {
            term_puts("no network: e1000 card not found (is -device e1000 set?)\n");
            return -1;
        }
        if (r != 0) {
            term_puts("no network: DHCP failed\n");
            return -1;
        }
    }
    uint8_t gw[6];
    if (net_arp(server_ip, gw) != 0) {
        term_puts("ARP: server did not answer\n");
        return -1;
    }
    net_ready = 1;
    char ipstr[16];
    net_ip_str(net_local_ip(), ipstr);
    term_puts("network up: ip ");
    term_puts(ipstr);
    term_putc('\n');
    return 0;
}

// download a path from the server; returns body length or -1
static int http_get(const char *path, uint8_t *body, uint32_t max)
{
    if (tcp_connect(server_ip, server_port, 4000) != 0) {
        term_puts("connect failed (is spgk-server running on the host?)\n");
        return -1;
    }
    char req[128];
    char *p = req;
    const char *s;
    s = "GET ";  while (*s) *p++ = *s++;
    s = path;     while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: spgk\r\n\r\n"; while (*s) *p++ = *s++;
    tcp_send((uint8_t *)req, (uint32_t)(p - req));

    static uint8_t buf[HTTP_MAX];        // whole response
    uint32_t total = 0;
    uint64_t start = timer_uptime_ms();
    while (total < HTTP_MAX && timer_uptime_ms() - start < 6000) {
        int n = tcp_recv(buf + total, HTTP_MAX - total, 1500);
        if (n > 0) {
            total += (uint32_t)n;
            start = timer_uptime_ms();   // data keeps coming? keep waiting
        } else if (n == -2)
            break;                       // server closed
    }
    tcp_close();
    if (!total)
        return -1;

    // find end of headers
    uint32_t body_off = 0;
    for (uint32_t i = 0; i + 3 < total; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            body_off = i + 4;
            break;
        }
    if (!body_off)
        return -1;                       // malformed response
    // check "200"
    if (buf[9] != '2' || buf[10] != '0' || buf[11] != '0')
        return -2;                       // 404 or similar
    uint32_t len = total - body_off;
    if (len > max) len = max;
    for (uint32_t i = 0; i < len; i++)
        body[i] = buf[body_off + i];
    return (int)len;
}

void cmd_netinfo(void)
{
    if (ensure_net() != 0)
        return;
    char ipstr[16];
    net_ip_str(net_local_ip(), ipstr);
    term_puts("ip      : ");
    term_puts(ipstr);
    term_putc('\n');
    const uint8_t *m = net_our_mac();
    term_puts("nic mac : ");
    for (int i = 0; i < 6; i++) {
        const char *hex = "0123456789ABCDEF";
        term_putc(hex[m[i] >> 4]);
        term_putc(hex[m[i] & 0xF]);
        if (i < 5) term_putc(':');
    }
    term_putc('\n');
    char sip[16];
    net_ip_str(server_ip, sip);
    term_puts("server  : ");
    term_puts(sip);
    term_puts(":8080 (spgk)\n");
}

void cmd_getspgk(const char *arg)
{
    static uint8_t body[HTTP_MAX];
    if (ensure_net() != 0)
        return;

    // list?
    if (!arg || !arg[0] || str_eq(arg, "list")) {
        int n = http_get("/index", body, HTTP_MAX);
        if (n < 0) {
            term_puts("getspgk: could not fetch the package list\n");
            return;
        }
        term_puts("packages on the server:\n");
        for (int i = 0; i < n; i++)
            term_putc(body[i]);
        if (n && body[n - 1] != '\n')
            term_putc('\n');
        return;
    }

    // server <ip>: point getspgk at a real machine on the LAN
    if (str_starts(arg, "server ") == 0) {
        uint32_t ip = parse_ip(arg + 7);
        if (!ip) {
            term_puts("usage: getspgk server <ip like 192.168.1.20>\n");
            return;
        }
        server_ip = ip;
        net_ready = 0;                   // re-resolve ARP for the new server
        char sip[16];
        net_ip_str(server_ip, sip);
        term_puts("spgk server set to ");
        term_puts(sip);
        term_puts(":8080\n");
        return;
    }

    // install <pkg>?
    const char *pkg = arg;
    if (str_starts(arg, "install ") == 0)
        pkg = arg + 8;
    if (!pkg[0]) {
        term_puts("usage: getspgk install <package>\n");
        return;
    }
    char path[64];
    char *p = path;
    const char *s = "/";
    while (*s) *p++ = *s++;
    s = pkg;
    while (*s) *p++ = *s++;
    *p = 0;
    term_puts("downloading ");
    term_puts(pkg);
    term_puts("...\n");
    int n = http_get(path, body, HTTP_MAX);
    if (n == -2) {
        term_puts("getspgk: no such package: ");
        term_puts(pkg);
        term_putc('\n');
        return;
    }
    if (n < 0) {
        term_puts("getspgk: download failed\n");
        return;
    }
    // save into ramfs as <pkg> (pkg names include their extension)
    if (ramfs_write(pkg, (const char *)body, (uint32_t)n) != 0) {
        term_puts("getspgk: ramfs full\n");
        return;
    }
    term_puts("installed ");
    term_puts(pkg);
    term_puts(" (");
    print_dec((uint32_t)n);
    term_puts(" bytes) into ramfs -- try: cat ");
    term_puts(pkg);
    term_putc('\n');
}
