#include "getspgk.h"
#include "net.h"
#include "nic.h"
#include "dns.h"
#include "http.h"
#include "ramfs.h"
#include "term.h"

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

void cmd_netinfo(void)
{
    if (http_ensure_net() != 0)
        return;
    char ipstr[16];
    net_ip_str(net_local_ip(), ipstr);
    term_puts("nic     : ");
    term_puts(nic_name());
    term_putc('\n');
    term_puts("ip      : ");
    term_puts(ipstr);
    term_putc('\n');
    net_ip_str(net_gateway(), ipstr);
    term_puts("gateway : ");
    term_puts(ipstr);
    term_putc('\n');
    net_ip_str(dns_server(), ipstr);
    term_puts("dns     : ");
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
    net_ip_str(http_server_ip(), sip);
    term_puts("server  : ");
    term_puts(sip);
    term_puts(":8080 (spgk)\n");
}

void cmd_getspgk(const char *arg)
{
    static uint8_t body[HTTP_MAX];
    if (http_ensure_net() != 0)
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
        http_set_server(ip, 8080);
        char sip[16];
        net_ip_str(http_server_ip(), sip);
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
    // save into ramfs at /<pkg> (absolute path, pkg keeps its extension)
    if (ramfs_write(path, (const char *)body, (uint32_t)n) != 0) {
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
