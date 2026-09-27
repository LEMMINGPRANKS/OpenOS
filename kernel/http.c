#include "http.h"
#include "net.h"
#include "nic.h"
#include "tcp.h"
#include "dns.h"
#include "term.h"
#include "timer.h"
#include "heap.h"
#include "settings.h"

#define DEFAULT_IP   IP(10,0,2,2)   // QEMU user-net gateway = host
#define DEFAULT_PORT 8080

static int net_ready;
static uint32_t server_ip = DEFAULT_IP;   // change with http_set_server
static uint16_t server_port = DEFAULT_PORT;

int http_set_server(uint32_t ip, uint16_t port)
{
    server_ip = ip;
    server_port = port;
    return 0;
}

uint32_t http_server_ip(void)   { return server_ip; }
uint16_t http_server_port(void) { return server_port; }

void http_net_forget(void) { net_ready = 0; }

static int parse_host(const char *host, int n, uint32_t *ip, uint16_t *port);

// the "server=ip:port" setting wins over the default, applied once at the
// first bring-up (and again after settings changes call http_net_forget)
static void apply_server_setting(void)
{
    static int applied;
    if (applied)
        return;
    applied = 1;
    char v[32];
    if (settings_get("server", v, sizeof v) != 0)
        return;
    int n = 0;
    while (v[n] && v[n] != ' ') n++;    // value should be a clean ip:port
    uint32_t ip;
    uint16_t port;
    if (parse_host(v, n, &ip, &port) == 0)
        http_set_server(ip, port);
}

int http_ensure_net(void)
{
    apply_server_setting();
    if (net_ready)
        return 0;
    if (!net_up()) {
        term_puts("bringing up the network (NIC + DHCP)...\n");
        int r = net_init();
        if (r == -1) {
            term_puts("no network: no supported network card found\n");
            return -1;
        }
        if (r != 0) {
            term_puts("no network: DHCP failed\n");
            return -1;
        }
    }
    // no ARP for the package server here: it only exists on QEMU user-net,
    // and every other destination is reached (and ARPed) through the
    // next hop when the first packet goes out
    net_ready = 1;
    char ipstr[16];
    net_ip_str(net_local_ip(), ipstr);
    term_puts("network up: ");
    term_puts(nic_name());
    term_puts(", ip ");
    term_puts(ipstr);
    term_putc('\n');
    return 0;
}

// one full request/response round trip over a fresh connection.
// Returns body length, -1 network failure, -2 server said not-2xx/3xx.
static int http_request(uint32_t ip, uint16_t port,
                        const char *req, uint32_t reqlen,
                        uint8_t *body, uint32_t max)
{
    if (tcp_connect(ip, port, 4000) != 0)
        return -1;
    tcp_send((uint8_t *)req, reqlen);

    // whole response: heap buffer so big downloads (kernel updates)
    // aren't stuck at HTTP_MAX
    uint32_t cap = max + 4096;
    uint8_t *buf = kmalloc(cap);
    if (!buf)
        return -1;
    uint32_t total = 0;
    uint64_t start = timer_uptime_ms();
    while (total < cap && timer_uptime_ms() - start < 6000) {
        int n = tcp_recv(buf + total, cap - total, 1500);
        if (n > 0) {
            total += (uint32_t)n;
            start = timer_uptime_ms();   // data keeps coming? keep waiting
        } else if (n == -2)
            break;                       // server closed
    }
    tcp_close();
    if (!total) {
        kfree(buf);
        return -1;
    }

    // find end of headers
    uint32_t body_off = 0;
    for (uint32_t i = 0; i + 3 < total; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            body_off = i + 4;
            break;
        }
    if (!body_off) {
        kfree(buf);
        return -1;                       // malformed response
    }
    // "200" / "204" / "303" ... all fine, 4xx/5xx are not
    if (buf[9] != '2' && buf[9] != '3') {
        kfree(buf);
        return -2;
    }
    uint32_t len = total - body_off;
    if (len > max) len = max;
    for (uint32_t i = 0; i < len; i++)
        body[i] = buf[body_off + i];
    kfree(buf);
    return (int)len;
}

int http_get(const char *path, uint8_t *body, uint32_t max)
{
    char req[160];
    char *p = req;
    const char *s;
    s = "GET ";  while (*s) *p++ = *s++;
    s = path;     while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: spgk\r\n\r\n"; while (*s) *p++ = *s++;
    return http_request(server_ip, server_port, req, (uint32_t)(p - req),
                        body, max);
}

// "10.0.2.2", "example.com", "example.com:80" -> ip + port. Returns 0 on ok.
static int parse_host(const char *host, int n, uint32_t *ip, uint16_t *port)
{
    uint16_t p = 0;
    int pn = n;
    for (int i = 0; i < n; i++)
        if (host[i] == ':') { pn = i; break; }
    if (pn < n) {
        for (int i = pn + 1; i < n; i++)
            if (host[i] >= '0' && host[i] <= '9')
                p = (uint16_t)(p * 10 + (uint16_t)(host[i] - '0'));
            else
                return -1;
        if (!p)
            return -1;
    }

    // dotted quad? then no DNS needed
    int dots = 0, ok = 1;
    for (int i = 0; i < pn; i++)
        if (host[i] == '.')
            dots++;
        else if (host[i] < '0' || host[i] > '9')
            ok = 0;
    if (ok && dots == 3) {
        uint32_t v = 0;
        int shift = 24;
        int oct = 0, any = 0;
        for (int i = 0; i <= pn; i++) {
            if (host[i] == '.' || i == pn) {
                if (!any || oct > 255)
                    return -1;
                v |= (uint32_t)oct << shift;
                shift -= 8;
                oct = 0; any = 0;
            } else {
                oct = oct * 10 + (host[i] - '0');
                any = 1;
            }
        }
        *ip = v;
        *port = p ? p : 80;
        return 0;
    }

    // a name: DNS it (null-terminated copy)
    char name[64];
    if (pn >= (int)sizeof name)
        return -1;
    for (int i = 0; i < pn; i++)
        name[i] = host[i];
    name[pn] = 0;
    uint32_t r = dns_resolve(name);
    if (!r)
        return -1;
    *ip = r;
    *port = p ? p : 80;
    return 0;
}

int http_get_url(const char *url, uint8_t *body, uint32_t max)
{
    if (http_ensure_net() != 0)
        return -1;
    const char *u = url;
    if (u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' &&
        u[4] == ':' && u[5] == '/' && u[6] == '/')
        u += 7;
    int n = 0;
    while (u[n] && u[n] != '/' && u[n] != ':')
        n++;
    // include :port in the host span for parse_host
    int span = n;
    while (u[span] && u[span] != '/')
        span++;
    uint32_t ip;
    uint16_t port;
    if (parse_host(u, span, &ip, &port) != 0) {
        term_puts("could not resolve that address\n");
        return -1;
    }
    const char *path = u[span] ? u + span : "/";

    char req[192];
    char *p = req;
    const char *s;
    s = "GET ";  while (*s) *p++ = *s++;
    s = path;     while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: "; while (*s) *p++ = *s++;
    for (int i = 0; i < span && p < req + sizeof req - 32; i++)
        *p++ = u[i];
    s = "\r\n\r\n"; while (*s) *p++ = *s++;
    return http_request(ip, port, req, (uint32_t)(p - req), body, max);
}

// POST text=... to our own server; the reply body comes back (the server
// answers with the refreshed page)
int http_post(const char *path, const char *text,
              uint8_t *out, uint32_t max)
{
    if (http_ensure_net() != 0)
        return -1;
    char req[256];
    char *p = req;
    const char *s;
    s = "POST "; while (*s) *p++ = *s++;
    s = path;    while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\nHost: spgk\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: "; while (*s) *p++ = *s++;
    int tlen = 0;
    while (text[tlen]) tlen++;
    // body is "text=" + urlencoded text (+5)
    int clen = 5 + tlen;
    if (clen > 999 || p + 32 + clen > req + sizeof req)
        return -1;
    char num[8];
    int nl = 0;
    int c2 = clen;
    if (!c2) num[nl++] = '0';
    while (c2) { num[nl++] = (char)('0' + c2 % 10); c2 /= 10; }
    while (nl) *p++ = num[--nl];
    s = "\r\n\r\ntext="; while (*s) *p++ = *s++;
    for (int i = 0; i < tlen; i++)
        *p++ = text[i] == ' ' ? '+' : text[i];
    return http_request(server_ip, server_port, req, (uint32_t)(p - req),
                        out, max);
}
