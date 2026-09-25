#include "tcp.h"
#include "net.h"
#include "e1000.h"
#include "timer.h"

struct tcp_hdr {
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  doff;                      // data offset (top nibble) + flags byte
    uint8_t  flags;
    uint16_t win, csum, urg;
} __attribute__((packed));

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

static uint32_t rem_ip;
static uint16_t rem_port, loc_port;
static uint32_t seq_num, ack_num;
static int connected;

static uint16_t htons_(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t ntohs_(uint16_t v) { return htons_(v); }
static uint32_t htonl_(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | (v >> 24);
}
static uint32_t ntohl_(uint32_t v) { return htonl_(v); }

int tcp_connected(void) { return connected; }
uint32_t tcp_remote_ip(void) { return rem_ip; }

static uint16_t tcp_checksum(const struct tcp_hdr *th, const uint8_t *data,
                             uint32_t dlen)
{
    // pseudo header: src ip, dst ip, zero, proto, tcp length
    uint32_t tcp_len = 20 + dlen;
    uint32_t sum = 0;
    uint32_t src = htonl_(net_local_ip());
    uint32_t dst = htonl_(rem_ip);
    sum += (src >> 16) & 0xFFFF; sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF; sum += dst & 0xFFFF;
    sum += 6;                            // protocol
    sum += tcp_len;

    const uint8_t *p = (const uint8_t *)th;
    for (uint32_t i = 0; i + 1 < 20; i += 2)
        sum += (uint32_t)p[i] << 8 | p[i + 1];
    for (uint32_t i = 0; i + 1 < dlen; i += 2)
        sum += (uint32_t)data[i] << 8 | data[i + 1];
    if (dlen & 1)
        sum += (uint32_t)data[dlen - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_send_seg(uint8_t flags, const uint8_t *data, uint32_t len)
{
    struct tcp_hdr th;
    th.sport = htons_(loc_port);
    th.dport = htons_(rem_port);
    th.seq = htonl_(seq_num);
    th.ack = htonl_(ack_num);
    th.doff = (20 / 4) << 4;
    th.flags = flags;
    th.win = htons_(4096);
    th.csum = 0;
    th.urg = 0;
    th.csum = tcp_checksum(&th, data, len);
    net_ip_send((uint8_t *)&th, 20, data, len, rem_ip, 6);
}

// wait for a segment matching our ports; fills header + data
struct rxseg {
    struct tcp_hdr hdr;
    uint8_t body[E1000_MTU];
    uint32_t body_len;
};

static int wait_seg(struct rxseg *out, uint32_t timeout_ms)
{
    static uint8_t buf[E1000_MTU];
    uint64_t start = timer_uptime_ms();
    for (;;) {
        int n = net_ip_poll_tcp(buf, E1000_MTU, 0);
        if (n >= 20) {
            struct tcp_hdr *h = (struct tcp_hdr *)buf;
            if (ntohs_(h->dport) == loc_port && ntohs_(h->sport) == rem_port) {
                out->hdr = *h;
                uint32_t doff = (uint32_t)(h->doff >> 4) * 4;
                uint32_t blen = (uint32_t)n - doff;
                if (blen > E1000_MTU) blen = E1000_MTU;
                for (uint32_t i = 0; i < blen; i++)
                    out->body[i] = buf[doff + i];
                out->body_len = blen;
                return (int)blen;
            }
        }
        if (timer_uptime_ms() - start > timeout_ms)
            return -1;
        __asm__ volatile ("hlt");
    }
}

int tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms)
{
    static struct rxseg r;               // too big for stack? static to be safe
    rem_ip = ip;
    rem_port = port;
    loc_port = (uint16_t)(10000 + (timer_uptime_ms() & 0xFFF));
    seq_num = timer_uptime_ms() * 9 + 1;
    ack_num = 0;
    connected = 0;

    tcp_send_seg(SYN, 0, 0);
    for (;;) {
        int n = wait_seg(&r, timeout_ms);
        if (n < 0)
            return -1;
        if (r.hdr.flags & (SYN | ACK)) {
            ack_num = ntohl_(r.hdr.seq) + 1;
            seq_num += 1;
            tcp_send_seg(ACK, 0, 0);
            connected = 1;
            return 0;
        }
        if (r.hdr.flags & (SYN)) {       // SYN without ACK (weird but reply)
            ack_num = ntohl_(r.hdr.seq) + 1;
            seq_num += 1;
            tcp_send_seg(SYN | ACK, 0, 0);
            continue;
        }
        if (r.hdr.flags & RST)
            return -2;
    }
}

int tcp_send(const uint8_t *data, uint32_t len)
{
    if (!connected)
        return -1;
    tcp_send_seg(PSH | ACK, data, len);
    seq_num += len;
    return 0;
}

int tcp_recv(uint8_t *buf, uint32_t max, uint32_t timeout_ms)
{
    static struct rxseg r;
    if (!connected)
        return -1;
    int n = wait_seg(&r, timeout_ms);
    if (n < 0)
        return -1;                       // timeout, still connected
    if (r.hdr.flags & RST) {
        connected = 0;
        return -2;
    }
    if (r.body_len) {
        ack_num = ntohl_(r.hdr.seq) + r.body_len;
        tcp_send_seg(ACK, 0, 0);
        uint32_t len = r.body_len > max ? max : r.body_len;
        for (uint32_t i = 0; i < len; i++)
            buf[i] = r.body[i];
        return (int)len;
    }
    if (r.hdr.flags & (FIN | ACK)) {     // server said goodbye
        ack_num = ntohl_(r.hdr.seq) + 1;
        tcp_send_seg(FIN | ACK, 0, 0);
        seq_num += 1;
        connected = 0;
        return -2;                       // closed
    }
    return 0;                            // bare ACK, no data
}

void tcp_close(void)
{
    if (!connected)
        return;
    tcp_send_seg(FIN | ACK, 0, 0);
    seq_num += 1;
    connected = 0;
    struct rxseg r;
    wait_seg(&r, 300);                   // swallow the final ACK
}
