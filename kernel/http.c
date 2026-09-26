#include "http.h"
#include "net.h"
#include "tcp.h"
#include "term.h"
#include "timer.h"

#define DEFAULT_IP   IP(10,0,2,2)   // QEMU user-net gateway = host
#define DEFAULT_PORT 8080

static int net_ready;
static uint32_t server_ip = DEFAULT_IP;   // change with http_set_server
static uint16_t server_port = DEFAULT_PORT;

int http_set_server(uint32_t ip, uint16_t port)
{
    server_ip = ip;
    server_port = port;
    net_ready = 0;                         // re-resolve ARP for the new server
    return 0;
}

uint32_t http_server_ip(void)   { return server_ip; }
uint16_t http_server_port(void) { return server_port; }

int http_ensure_net(void)
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

int http_get(const char *path, uint8_t *body, uint32_t max)
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
